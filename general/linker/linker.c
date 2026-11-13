/*
 * linker.c - static ELF64/AArch64 executable linker for the Tracey toolchain.
 *
 * The linker consumes relocatable ELF64 AArch64 objects produced by the
 * Tracey assembler (general/assembler/assembler.c) and emits a statically
 * linked ET_EXEC image. No dynamic loader, PLT, GOT, common-symbol merging or
 * section garbage collection is implemented: the assembler only emits a flat
 * .text section, a symbol table and R_AARCH64_CALL26/R_AARCH64_JUMP26/
 * R_AARCH64_CONDBR19 relocations, and this file only handles that subset.
 *
 * The produced image uses a fixed memory layout:
 *
 *   0x400000  file offset 0        ELF header and program header
 *   0x401000  file offset 0x1000   .text (entry point lives here)
 *
 * All input .text sections are concatenated into that single executable
 * segment, every input symbol is mapped to its final virtual address and the
 * recorded relocations are patched in place. Section headers and a symbol
 * table are appended after the code so the result can be inspected with
 * readelf/objdump; they are not part of the loaded segment.
 *
 * Memory ownership rules used throughout this file:
 *
 *   - struct link_object owns its text bytes, its symbol name strings, its
 *     relocation symbol name strings and its debug section copies. All of
 *     them are released by link_object_free().
 *   - The resolved symbol table borrows the name pointers stored in the
 *     link_object array. They stay valid until the objects are freed at the
 *     end of tracey_link_elf_aarch64(), which is after the last use.
 *   - The string tables and symbol array built for the output file own their
 *     bytes and are released before every return path.
 *
 * File offsets and addresses are handled as 64-bit quantities because ELF64
 * fields are 64-bit; malformed input must never make the arithmetic wrap into
 * an in-bounds but incorrect access.
 */

#include "tracey/elf.h"
#include "tracey/linker.h"

#include <inttypes.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Fixed layout of the produced executable. */
#define LINK_EHDR_SIZE ((uint64_t)64)
#define LINK_PHDR_SIZE ((uint64_t)56)
#define LINK_SECTION_HEADER_SIZE ((uint64_t)64)
#define LINK_SYMBOL_ENTRY_SIZE ((uint64_t)24)
#define LINK_RELOCA_ENTRY_SIZE ((uint64_t)24)
#define LINK_TEXT_FILE_OFFSET ((uint64_t)0x1000)
#define LINK_BASE_VA ((uint64_t)0x400000)
#define LINK_CODE_BASE_VA (LINK_BASE_VA + LINK_TEXT_FILE_OFFSET)
#define LINK_PROGRAM_ALIGNMENT ((uint64_t)0x1000)

/* Section indices used when the output section headers are emitted. */
#define LINK_INDEX_TEXT ((uint32_t)1)
#define LINK_INDEX_SYMTAB ((uint32_t)2)
#define LINK_INDEX_STRTAB ((uint32_t)3)
#define LINK_INDEX_SHSTRTAB ((uint32_t)4)

/* Alignment of the concatenated code and of the auxiliary output sections. */
#define LINK_INSN_ALIGNMENT ((uint64_t)4)
#define LINK_SECTION_ALIGNMENT ((uint64_t)8)
#define LINK_TEXT_SHADDR_ALIGNMENT ((uint64_t)16)

/*
 * Twelve-byte entry stub:
 *
 *   bl   main          ; 0x94000000 | imm26
 *   mov  x8, #93       ; __NR_exit on AArch64 Linux
 *   svc  #0
 *
 * The stub is only emitted when no input defines _start. The exit status is
 * whatever main leaves in x0, matching the C ABI for an int return value.
 */
#define LINK_STARTUP_STUB_SIZE ((uint64_t)12)
#define LINK_STARTUP_MOV_X8 ((uint32_t)0xD2800BA8u)
#define LINK_STARTUP_SVC0 ((uint32_t)0xD4000001u)
#define LINK_BL_OPCODE ((uint32_t)0x94000000u)

/* Relocation types handled by the linker. */
#define LINK_R_AARCH64_CONDBR19 ((uint32_t)280)
#define LINK_R_AARCH64_JUMP26 ((uint32_t)282)
#define LINK_R_AARCH64_CALL26 ((uint32_t)283)

/* Encoding masks and signed byte ranges for the supported relocations. */
#define LINK_JUMP26_MASK ((uint32_t)0x03FFFFFFu)
#define LINK_JUMP26_MIN_DELTA ((int64_t)-0x08000000)
#define LINK_JUMP26_MAX_DELTA ((int64_t)0x07FFFFFC)

#define LINK_CONDBR19_MASK ((uint32_t)0x0007FFFFu)
#define LINK_CONDBR19_FIELD ((uint32_t)0x00FFFFE0u)
#define LINK_CONDBR19_SHIFT ((uint32_t)5)
#define LINK_CONDBR19_MIN_DELTA ((int64_t)-0x00100000)
#define LINK_CONDBR19_MAX_DELTA ((int64_t)0x000FFFFC)

/* Growable byte buffer. data is NULL while size is 0. */
struct link_buffer
{
    unsigned char *data;
    size_t size;
    size_t capacity;
};

/* Bounds-checked little-endian reader over a complete input file. */
struct link_reader
{
    const unsigned char *data;
    size_t size;
};

/* Section contents borrowed from the input file buffer. */
struct link_section
{
    const unsigned char *data;
    size_t size;
};

/*
 * Section header fields needed by this linker. Only the members the linker
 * consumes are kept; sh_flags, sh_addr and sh_entsize are irrelevant because
 * the assembler always emits fixed-size entries and a single flat .text.
 */
struct link_section_header
{
    uint32_t name;
    uint32_t type;
    uint64_t offset;
    uint64_t size;
    uint32_t link;
};

/* Symbol of one input object. name is owned by the object. */
struct link_symbol
{
    char *name;
    uint64_t value;
    bool is_global;
    bool is_defined;
};

/* Relocation of one input object. symbol is owned by the object. */
struct link_reloc
{
    uint64_t offset;
    uint32_t type;
    char *symbol;
};

/* One parsed relocatable input object. */
struct link_object
{
    unsigned char *text;
    size_t text_size;
    struct link_symbol *symbols;
    size_t symbol_count;
    size_t symbol_capacity;
    struct link_reloc *relocs;
    size_t reloc_count;
    size_t reloc_capacity;
    unsigned char *debug_line;
    size_t debug_line_size;
    unsigned char *debug_info;
    size_t debug_info_size;
    unsigned char *debug_abbrev;
    size_t debug_abbrev_size;
};

/*
 * Entry of the link-wide symbol table. name points into the symbol array of
 * one link_object and is therefore borrowed, not owned.
 */
struct link_resolved_symbol
{
    const char *name;
    uint64_t address;
    bool is_global;
};

struct link_symbol_table
{
    struct link_resolved_symbol *items;
    size_t count;
    size_t capacity;
};

/* Symbol as it will be written to the output .symtab. name is borrowed. */
struct link_output_symbol
{
    const char *name;
    uint64_t address;
    bool is_global;
};

/*
 * link_diagnostic - report a linker failure on stderr.
 *
 * fmt and the variadic arguments follow printf conventions. Every message is
 * prefixed with the toolchain identifier so linker failures can be told apart
 * from assembler or code generator messages. No trailing newline is added;
 * callers terminate their format strings with "\n".
 */
static void link_diagnostic(const char *fmt, ...)
{
    va_list args;

    fputs("tracey: linker: ", stderr);
    va_start(args, fmt);
    vfprintf(stderr, fmt, args);
    va_end(args);
}

/*
 * link_align_up - round value up to the next multiple of alignment.
 *
 * alignment must be a power of two larger than zero. The caller guarantees
 * that the addition cannot overflow because every value used here is a small
 * file offset.
 */
static uint64_t link_align_up(uint64_t value, uint64_t alignment)
{
    uint64_t remainder = value % alignment;

    return remainder == 0 ? value : value + (alignment - remainder);
}

/*
 * buffer_init - place a buffer in the empty state.
 */
static void buffer_init(struct link_buffer *buffer)
{
    buffer->data = NULL;
    buffer->size = 0;
    buffer->capacity = 0;
}

/*
 * buffer_free - release the storage of a buffer and reset it.
 */
static void buffer_free(struct link_buffer *buffer)
{
    free(buffer->data);
    buffer_init(buffer);
}

/*
 * grow_array - enlarge a dynamic array so that it can hold needed elements.
 *
 * p_items points to the array pointer, p_capacity to its current capacity in
 * elements. item_size is the size of one element in bytes. The capacity grows
 * geometrically to keep appending amortised constant; the array contents are
 * preserved. A zero capacity is valid and causes an initial allocation.
 *
 * Returns false when the requested size is not representable or when the
 * allocation fails; the array is then left untouched.
 */
static bool grow_array(void **p_items, size_t *p_capacity, size_t needed, size_t item_size)
{
    size_t capacity;
    void *grown;

    if (needed <= *p_capacity)
        return true;
    if (item_size == 0 || needed > SIZE_MAX / item_size)
        return false;
    capacity = *p_capacity == 0 ? 8 : *p_capacity;
    while (capacity < needed)
    {
        if (capacity > SIZE_MAX / 2)
        {
            capacity = needed;
            break;
        }
        capacity *= 2;
    }
    if (capacity > SIZE_MAX / item_size)
        return false;
    grown = realloc(*p_items, capacity * item_size);
    if (grown == NULL)
        return false;
    *p_items = grown;
    *p_capacity = capacity;
    return true;
}

/*
 * buffer_reserve - ensure that extra more bytes fit into the buffer.
 *
 * The buffer size is not changed; only the allocation is grown. Returns false
 * on allocation failure or size overflow.
 */
static bool buffer_reserve(struct link_buffer *buffer, size_t extra)
{
    if (extra > SIZE_MAX - buffer->size)
        return false;
    return grow_array((void **)&buffer->data, &buffer->capacity, buffer->size + extra, 1);
}

/*
 * buffer_append - append size bytes to the buffer.
 *
 * data may be NULL only when size is zero. The bytes are copied, so the caller
 * keeps ownership of data. Returns false on allocation failure.
 */
static bool buffer_append(struct link_buffer *buffer, const void *data, size_t size)
{
    if (size == 0)
        return true;
    if (!buffer_reserve(buffer, size))
        return false;
    memcpy(buffer->data + buffer->size, data, size);
    buffer->size += size;
    return true;
}

/*
 * buffer_append_zeros - append count zero bytes to the buffer.
 *
 * Used to keep instruction-aligned code when consecutive input sections have
 * sizes that are not multiples of four.
 */
static bool buffer_append_zeros(struct link_buffer *buffer, size_t count)
{
    if (count == 0)
        return true;
    if (!buffer_reserve(buffer, count))
        return false;
    memset(buffer->data + buffer->size, 0, count);
    buffer->size += count;
    return true;
}

/*
 * link_read_file - read a complete file into a buffer.
 *
 * The file is opened in binary mode and read in chunks, so the size does not
 * have to be known in advance and files larger than a long can be handled.
 * Returns false when the file cannot be opened or a read error occurs; a
 * partially filled buffer is left for the caller to release.
 */
static bool link_read_file(const char *path, struct link_buffer *out)
{
    FILE *file;
    unsigned char chunk[8192];
    size_t got;

    file = fopen(path, "rb");
    if (file == NULL)
        return false;
    while ((got = fread(chunk, 1, sizeof(chunk), file)) > 0)
    {
        if (!buffer_append(out, chunk, got))
        {
            fclose(file);
            return false;
        }
    }
    if (ferror(file))
    {
        fclose(file);
        return false;
    }
    fclose(file);
    return true;
}

/*
 * reader_init - bind a reader to a complete in-memory file image.
 */
static void reader_init(struct link_reader *reader, const unsigned char *data, size_t size)
{
    reader->data = data;
    reader->size = size;
}

/*
 * reader_range_ok - check that [offset, offset + length) lies inside the file.
 *
 * The comparison is written so that offset + length cannot overflow: offset is
 * first bounded by the file size, then length by the remaining bytes.
 */
static bool reader_range_ok(const struct link_reader *reader, uint64_t offset, uint64_t length)
{
    if (offset > (uint64_t)reader->size)
        return false;
    return length <= (uint64_t)reader->size - offset;
}

/*
 * Little-endian scalar readers. Each returns false when the requested field is
 * not completely inside the file image. The raw bytes are never reinterpreted
 * through a struct pointer, so the result does not depend on host alignment or
 * host endianness.
 */
static bool reader_u16(const struct link_reader *reader, uint64_t offset, uint16_t *out)
{
    if (!reader_range_ok(reader, offset, 2))
        return false;
    *out = (uint16_t)((uint16_t)reader->data[offset] |
                      (uint16_t)((uint16_t)reader->data[offset + 1] << 8));
    return true;
}

static bool reader_u32(const struct link_reader *reader, uint64_t offset, uint32_t *out)
{
    if (!reader_range_ok(reader, offset, 4))
        return false;
    *out = (uint32_t)reader->data[offset] | ((uint32_t)reader->data[offset + 1] << 8) |
           ((uint32_t)reader->data[offset + 2] << 16) | ((uint32_t)reader->data[offset + 3] << 24);
    return true;
}

static bool reader_u64(const struct link_reader *reader, uint64_t offset, uint64_t *out)
{
    uint64_t value = 0;
    uint64_t i;

    if (!reader_range_ok(reader, offset, 8))
        return false;
    for (i = 0; i < 8; i++)
        value |= (uint64_t)reader->data[offset + i] << (8 * i);
    *out = value;
    return true;
}

/*
 * store_u32 - write a 32-bit value in little-endian order into raw bytes.
 */
static void store_u32(unsigned char *p, uint32_t value)
{
    p[0] = (unsigned char)(value & 0xFFu);
    p[1] = (unsigned char)((value >> 8) & 0xFFu);
    p[2] = (unsigned char)((value >> 16) & 0xFFu);
    p[3] = (unsigned char)((value >> 24) & 0xFFu);
}

/*
 * load_u32 - read a little-endian 32-bit value from raw bytes.
 *
 * The caller guarantees that four bytes are readable at p.
 */
static uint32_t load_u32(const unsigned char *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

/*
 * link_strdup - duplicate a NUL-terminated string on the heap.
 *
 * Returns NULL when the allocation fails. The caller owns the result and
 * releases it with free().
 */
static char *link_strdup(const char *text)
{
    size_t length = strlen(text);
    char *copy = (char *)malloc(length + 1);

    if (copy == NULL)
        return NULL;
    memcpy(copy, text, length + 1);
    return copy;
}

/*
 * section_from_header - resolve a section header into a view of the file image.
 *
 * The view for a zero-sized section has data == NULL. Returns false when the
 * section lies outside the file, which can only happen with malformed input.
 */
static bool section_from_header(const struct link_reader *reader,
                                const struct link_section_header *header,
                                struct link_section *out)
{
    if (header->size == 0)
    {
        out->data = NULL;
        out->size = 0;
        return true;
    }
    if (header->size > (uint64_t)SIZE_MAX)
        return false;
    if (!reader_range_ok(reader, header->offset, header->size))
        return false;
    out->data = reader->data + header->offset;
    out->size = (size_t)header->size;
    return true;
}

/*
 * section_string - fetch a NUL-terminated string from a string table section.
 *
 * index is a byte offset into the table. Returns false when the offset is out
 * of range or when the table does not terminate the string inside its own
 * bounds. The returned pointer is borrowed from the section.
 */
static bool section_string(const struct link_section *table, uint32_t index, const char **out)
{
    size_t i;

    if (table->data == NULL || (size_t)index >= table->size)
        return false;
    for (i = (size_t)index; i < table->size; i++)
    {
        if (table->data[i] == '\0')
        {
            *out = (const char *)table->data + index;
            return true;
        }
    }
    return false;
}

/*
 * section_name_is - compare the name referenced by a section header to a
 * literal.
 *
 * Returns false when the name cannot be resolved at all, which makes the input
 * malformed; true otherwise, with *p_match telling whether the name is equal.
 */
static bool section_name_is(const struct link_section *shstrtab, uint32_t name_index,
                            const char *literal, bool *p_match)
{
    const char *name;

    if (!section_string(shstrtab, name_index, &name))
        return false;
    *p_match = strcmp(name, literal) == 0;
    return true;
}

/*
 * link_object_free - release every allocation owned by one input object.
 *
 * The structure is left zeroed, so calling the function twice is harmless.
 */
static void link_object_free(struct link_object *object)
{
    size_t i;

    free(object->text);

    for (i = 0; i < object->symbol_count; i++)
        free(object->symbols[i].name);
    free(object->symbols);

    for (i = 0; i < object->reloc_count; i++)
        free(object->relocs[i].symbol);
    free(object->relocs);

    free(object->debug_line);
    free(object->debug_info);
    free(object->debug_abbrev);

    memset(object, 0, sizeof(*object));
}

/*
 * link_object_take_debug - replace a debug section copy with a new one.
 *
 * When several sections share a name the last non-empty one wins, matching the
 * assembler, which emits at most one of each. Returns false on allocation
 * failure; the previous copy is kept in that case.
 */
static bool link_object_take_debug(unsigned char **p_data, size_t *p_size,
                                   const struct link_section *section)
{
    unsigned char *copy;

    if (section->size == 0)
        return true;
    copy = (unsigned char *)malloc(section->size);
    if (copy == NULL)
        return false;
    memcpy(copy, section->data, section->size);
    free(*p_data);
    *p_data = copy;
    *p_size = section->size;
    return true;
}

/*
 * link_object_read - parse one relocatable ELF64 object.
 *
 * object must point to writable storage; it is zeroed before parsing and owns
 * every allocation made here. path is used only for diagnostics.
 *
 * The parser validates all header-derived offsets against the real file size
 * before using them, because a truncated or hostile object must not make the
 * linker read outside the file buffer.
 *
 * Required sections are .text, .symtab and .strtab. .rela.text is optional.
 * .debug_line, .debug_info and .debug_abbrev are copied when present so the
 * final executable keeps whatever debug data the assembler produced.
 *
 * Returns true on success. On failure a diagnostic has been printed and all
 * partial allocations have been released.
 */
static bool link_object_read(const char *path, struct link_object *object)
{
    struct link_buffer file;
    struct link_reader reader;
    struct link_section_header *sections = NULL;
    struct link_section shstrtab;
    struct link_section strtab;
    uint64_t section_offset = 0;
    uint16_t section_count = 0;
    uint16_t shstrtab_index = 0;
    size_t text_index = SIZE_MAX;
    size_t symtab_index = SIZE_MAX;
    size_t strtab_index = SIZE_MAX;
    size_t rela_index = SIZE_MAX;
    size_t i;
    bool ok = false;

    memset(object, 0, sizeof(*object));
    buffer_init(&file);
    shstrtab.data = NULL;
    shstrtab.size = 0;
    strtab.data = NULL;
    strtab.size = 0;

    if (!link_read_file(path, &file))
    {
        link_diagnostic("cannot read '%s'\n", path);
        goto done;
    }
    if (file.size < (size_t)LINK_EHDR_SIZE)
    {
        link_diagnostic("%s: file is too small to be an ELF64 object\n", path);
        goto done;
    }
    if (file.data[0] != 0x7f || file.data[1] != 'E' || file.data[2] != 'L' || file.data[3] != 'F')
    {
        link_diagnostic("%s: not an ELF object (bad identification bytes)\n", path);
        goto done;
    }
    if (file.data[4] != 2 || file.data[5] != 1)
    {
        link_diagnostic("%s: not a little-endian ELF64 object\n", path);
        goto done;
    }

    reader_init(&reader, file.data, file.size);
    (void)reader_u64(&reader, 40, &section_offset);
    (void)reader_u16(&reader, 60, &section_count);
    (void)reader_u16(&reader, 62, &shstrtab_index);

    if (section_count == 0)
    {
        link_diagnostic("%s: object has no section header table\n", path);
        goto done;
    }
    if (shstrtab_index >= section_count)
    {
        link_diagnostic("%s: section name table index is out of range\n", path);
        goto done;
    }
    if ((uint64_t)section_count > (UINT64_MAX - section_offset) / LINK_SECTION_HEADER_SIZE ||
        !reader_range_ok(&reader, section_offset,
                         (uint64_t)section_count * LINK_SECTION_HEADER_SIZE))
    {
        link_diagnostic("%s: section header table lies outside the file\n", path);
        goto done;
    }

    sections = (struct link_section_header *)calloc(section_count, sizeof(*sections));
    if (sections == NULL)
    {
        link_diagnostic("out of memory while reading '%s'\n", path);
        goto done;
    }
    for (i = 0; i < section_count; i++)
    {
        uint64_t base = section_offset + (uint64_t)i * LINK_SECTION_HEADER_SIZE;

        /* The whole table has been range-checked above. */
        (void)reader_u32(&reader, base, &sections[i].name);
        (void)reader_u32(&reader, base + 4, &sections[i].type);
        (void)reader_u64(&reader, base + 24, &sections[i].offset);
        (void)reader_u64(&reader, base + 32, &sections[i].size);
        (void)reader_u32(&reader, base + 40, &sections[i].link);
    }

    if (!section_from_header(&reader, &sections[shstrtab_index], &shstrtab))
    {
        link_diagnostic("%s: section name table lies outside the file\n", path);
        goto done;
    }

    /*
     * Locate the sections the linker understands. The names are compared
     * against the section name string table; an unresolvable name offset means
     * the object is malformed.
     */
    for (i = 0; i < section_count; i++)
    {
        bool match;

        if (!section_name_is(&shstrtab, sections[i].name, ".text", &match))
        {
            link_diagnostic("%s: section header %zu has an invalid name offset\n", path, i);
            goto done;
        }
        if (match)
            text_index = i;

        if (!section_name_is(&shstrtab, sections[i].name, ".symtab", &match))
        {
            link_diagnostic("%s: section header %zu has an invalid name offset\n", path, i);
            goto done;
        }
        if (match)
            symtab_index = i;

        if (!section_name_is(&shstrtab, sections[i].name, ".strtab", &match))
        {
            link_diagnostic("%s: section header %zu has an invalid name offset\n", path, i);
            goto done;
        }
        if (match)
            strtab_index = i;

        if (!section_name_is(&shstrtab, sections[i].name, ".rela.text", &match))
        {
            link_diagnostic("%s: section header %zu has an invalid name offset\n", path, i);
            goto done;
        }
        if (match)
            rela_index = i;

        if (!section_name_is(&shstrtab, sections[i].name, ".debug_line", &match))
        {
            link_diagnostic("%s: section header %zu has an invalid name offset\n", path, i);
            goto done;
        }
        if (match && sections[i].size > 0)
        {
            struct link_section debug;

            if (!section_from_header(&reader, &sections[i], &debug))
            {
                link_diagnostic("%s: .debug_line lies outside the file\n", path);
                goto done;
            }
            if (!link_object_take_debug(&object->debug_line, &object->debug_line_size, &debug))
            {
                link_diagnostic("out of memory while reading '%s'\n", path);
                goto done;
            }
        }

        if (!section_name_is(&shstrtab, sections[i].name, ".debug_info", &match))
        {
            link_diagnostic("%s: section header %zu has an invalid name offset\n", path, i);
            goto done;
        }
        if (match && sections[i].size > 0)
        {
            struct link_section debug;

            if (!section_from_header(&reader, &sections[i], &debug))
            {
                link_diagnostic("%s: .debug_info lies outside the file\n", path);
                goto done;
            }
            if (!link_object_take_debug(&object->debug_info, &object->debug_info_size, &debug))
            {
                link_diagnostic("out of memory while reading '%s'\n", path);
                goto done;
            }
        }

        if (!section_name_is(&shstrtab, sections[i].name, ".debug_abbrev", &match))
        {
            link_diagnostic("%s: section header %zu has an invalid name offset\n", path, i);
            goto done;
        }
        if (match && sections[i].size > 0)
        {
            struct link_section debug;

            if (!section_from_header(&reader, &sections[i], &debug))
            {
                link_diagnostic("%s: .debug_abbrev lies outside the file\n", path);
                goto done;
            }
            if (!link_object_take_debug(&object->debug_abbrev, &object->debug_abbrev_size, &debug))
            {
                link_diagnostic("out of memory while reading '%s'\n", path);
                goto done;
            }
        }
    }

    if (text_index == SIZE_MAX)
    {
        link_diagnostic("%s: object has no .text section\n", path);
        goto done;
    }

    {
        struct link_section text;

        if (!section_from_header(&reader, &sections[text_index], &text))
        {
            link_diagnostic("%s: .text lies outside the file\n", path);
            goto done;
        }
        if (text.size > 0)
        {
            object->text = (unsigned char *)malloc(text.size);
            if (object->text == NULL)
            {
                link_diagnostic("out of memory while reading '%s'\n", path);
                goto done;
            }
            memcpy(object->text, text.data, text.size);
            object->text_size = text.size;
        }
    }

    if (strtab_index != SIZE_MAX &&
        !section_from_header(&reader, &sections[strtab_index], &strtab))
    {
        link_diagnostic("%s: .strtab lies outside the file\n", path);
        goto done;
    }

    if (symtab_index != SIZE_MAX)
    {
        struct link_section symtab;
        size_t symbol_count;

        if (!section_from_header(&reader, &sections[symtab_index], &symtab))
        {
            link_diagnostic("%s: .symtab lies outside the file\n", path);
            goto done;
        }
        if (sections[symtab_index].type != TRACEY_ELF_SHT_SYMTAB)
        {
            link_diagnostic("%s: .symtab is not a symbol table section\n", path);
            goto done;
        }
        symbol_count = symtab.size / (size_t)LINK_SYMBOL_ENTRY_SIZE;
        for (i = 0; i < symbol_count; i++)
        {
            uint64_t base = sections[symtab_index].offset + (uint64_t)i * LINK_SYMBOL_ENTRY_SIZE;
            struct link_symbol symbol;
            uint32_t name_index = 0;
            uint16_t section_index = 0;
            unsigned char info;

            memset(&symbol, 0, sizeof(symbol));
            /*
             * The entry has been proven to lie inside the section, and the
             * section inside the file, so these reads cannot fail.
             */
            (void)reader_u32(&reader, base, &name_index);
            info = file.data[base + 4];
            (void)reader_u16(&reader, base + 6, &section_index);
            (void)reader_u64(&reader, base + 8, &symbol.value);

            symbol.is_global = ((unsigned char)(info >> 4) == TRACEY_ELF_STB_GLOBAL);
            symbol.is_defined = (section_index != TRACEY_ELF_SHN_UNDEF);

            if (name_index != 0)
            {
                const char *name;

                if (!section_string(&strtab, name_index, &name))
                {
                    link_diagnostic("%s: symbol %zu has an invalid name offset\n", path, i);
                    goto done;
                }
                symbol.name = link_strdup(name);
            }
            else
            {
                symbol.name = link_strdup("");
            }
            if (symbol.name == NULL)
            {
                link_diagnostic("out of memory while reading '%s'\n", path);
                goto done;
            }

            if (!grow_array((void **)&object->symbols, &object->symbol_capacity,
                            object->symbol_count + 1, sizeof(*object->symbols)))
            {
                free(symbol.name);
                link_diagnostic("out of memory while reading '%s'\n", path);
                goto done;
            }
            object->symbols[object->symbol_count++] = symbol;
        }
    }

    if (rela_index != SIZE_MAX)
    {
        struct link_section rela;
        struct link_section rela_symtab;
        struct link_section rela_strtab;
        uint32_t linked_symtab = sections[rela_index].link;
        size_t reloc_count;

        if (!section_from_header(&reader, &sections[rela_index], &rela))
        {
            link_diagnostic("%s: .rela.text lies outside the file\n", path);
            goto done;
        }
        if (linked_symtab == 0 || (size_t)linked_symtab >= (size_t)section_count)
        {
            link_diagnostic("%s: .rela.text refers to an invalid symbol table\n", path);
            goto done;
        }
        if (sections[linked_symtab].type != TRACEY_ELF_SHT_SYMTAB)
        {
            link_diagnostic("%s: .rela.text does not refer to a symbol table section\n", path);
            goto done;
        }
        if (!section_from_header(&reader, &sections[linked_symtab], &rela_symtab) ||
            sections[linked_symtab].link >= section_count ||
            !section_from_header(&reader, &sections[sections[linked_symtab].link], &rela_strtab))
        {
            link_diagnostic("%s: .rela.text symbol or string table lies outside the file\n", path);
            goto done;
        }

        reloc_count = rela.size / (size_t)LINK_RELOCA_ENTRY_SIZE;
        for (i = 0; i < reloc_count; i++)
        {
            uint64_t base = sections[rela_index].offset + (uint64_t)i * LINK_RELOCA_ENTRY_SIZE;
            struct link_reloc reloc;
            uint64_t info = 0;
            uint32_t symbol_index;
            uint32_t name_index = 0;
            uint64_t symbol_entry;

            memset(&reloc, 0, sizeof(reloc));
            (void)reader_u64(&reader, base, &reloc.offset);
            (void)reader_u64(&reader, base + 8, &info);
            reloc.type = (uint32_t)(info & 0xFFFFFFFFu);
            symbol_index = (uint32_t)(info >> 32);

            symbol_entry = (uint64_t)symbol_index * LINK_SYMBOL_ENTRY_SIZE;
            if (symbol_entry > (uint64_t)rela_symtab.size ||
                (uint64_t)rela_symtab.size - symbol_entry < LINK_SYMBOL_ENTRY_SIZE)
            {
                link_diagnostic("%s: relocation %zu refers to symbol %" PRIu32 " outside .symtab\n",
                                path, i, symbol_index);
                goto done;
            }
            (void)reader_u32(&reader, sections[linked_symtab].offset + symbol_entry, &name_index);

            if (name_index != 0)
            {
                const char *name;

                if (!section_string(&rela_strtab, name_index, &name))
                {
                    link_diagnostic("%s: relocation %zu has an invalid symbol name\n", path, i);
                    goto done;
                }
                reloc.symbol = link_strdup(name);
            }
            else
            {
                reloc.symbol = link_strdup("");
            }
            if (reloc.symbol == NULL)
            {
                link_diagnostic("out of memory while reading '%s'\n", path);
                goto done;
            }

            if (!grow_array((void **)&object->relocs, &object->reloc_capacity,
                            object->reloc_count + 1, sizeof(*object->relocs)))
            {
                free(reloc.symbol);
                link_diagnostic("out of memory while reading '%s'\n", path);
                goto done;
            }
            object->relocs[object->reloc_count++] = reloc;
        }
    }

    ok = true;

done:
    free(sections);
    buffer_free(&file);
    if (!ok)
        link_object_free(object);
    return ok;
}

/*
 * link_symbol_table_init - place the link-wide symbol table in the empty state.
 */
static void link_symbol_table_init(struct link_symbol_table *table)
{
    table->items = NULL;
    table->count = 0;
    table->capacity = 0;
}

/*
 * link_symbol_table_free - release the link-wide symbol table.
 *
 * The stored names are borrowed from the input objects and are therefore not
 * released here.
 */
static void link_symbol_table_free(struct link_symbol_table *table)
{
    free(table->items);
    link_symbol_table_init(table);
}

/*
 * link_symbol_table_find - look up a symbol by name.
 *
 * Returns the index of the first entry with a matching name, or SIZE_MAX when
 * the name is unknown. The comparison is byte-wise, like the std::map used by
 * the previous implementation, so the ordering and the matching rules are
 * unchanged. Linear search is adequate: objects contain at most a few thousand
 * symbols and the link is not a bottleneck.
 */
static size_t link_symbol_table_find(const struct link_symbol_table *table, const char *name)
{
    size_t i;

    for (i = 0; i < table->count; i++)
    {
        if (strcmp(table->items[i].name, name) == 0)
            return i;
    }
    return SIZE_MAX;
}

/*
 * link_symbol_table_add - add a symbol unless its name is already known.
 *
 * The first definition wins, which mirrors the map insertion policy of the
 * previous implementation. The name pointer is borrowed and must outlive the
 * table. Returns false only on allocation failure.
 */
static bool link_symbol_table_add(struct link_symbol_table *table, const char *name,
                                  uint64_t address, bool is_global)
{
    struct link_resolved_symbol *slot;

    if (link_symbol_table_find(table, name) != SIZE_MAX)
        return true;
    if (!grow_array((void **)&table->items, &table->capacity, table->count + 1,
                    sizeof(*table->items)))
        return false;
    slot = &table->items[table->count++];
    slot->name = name;
    slot->address = address;
    slot->is_global = is_global;
    return true;
}

/*
 * link_output_symbol_compare - qsort comparator ordering symbols by name.
 *
 * The names are plain byte strings. strcmp compares them as unsigned char,
 * which matches the byte-wise ordering the previous std::map based
 * implementation produced.
 */
static int link_output_symbol_compare(const void *lhs, const void *rhs)
{
    const struct link_output_symbol *left = (const struct link_output_symbol *)lhs;
    const struct link_output_symbol *right = (const struct link_output_symbol *)rhs;

    return strcmp(left->name, right->name);
}

/*
 * link_write_zeros - write count zero bytes to a stream.
 *
 * The padding is emitted in chunks so the stack usage stays constant. The
 * function returns false as soon as a short write is detected.
 */
static bool link_write_zeros(FILE *file, uint64_t count)
{
    static const unsigned char zeros[64] = {0};

    while (count > 0)
    {
        size_t chunk = count > (uint64_t)sizeof(zeros) ? sizeof(zeros) : (size_t)count;

        if (fwrite(zeros, 1, chunk, file) != chunk)
            return false;
        count -= (uint64_t)chunk;
    }
    return true;
}

/*
 * link_write_padding - pad the stream from cursor to target with zero bytes.
 *
 * cursor is the number of bytes already written and target the file offset the
 * next structure must start at. A target behind the cursor would indicate a
 * layout bug, so it is rejected instead of seeking backwards.
 */
static bool link_write_padding(FILE *file, uint64_t cursor, uint64_t target)
{
    if (target < cursor)
        return false;
    return link_write_zeros(file, target - cursor);
}

/*
 * link_write_bytes - write a whole block of bytes to a stream.
 *
 * data may be NULL only when size is zero. Returns false on a short write.
 */
static bool link_write_bytes(FILE *file, const void *data, size_t size)
{
    if (size == 0)
        return true;
    return fwrite(data, 1, size, file) == size;
}

/*
 * tracey_link_elf_aarch64 - see include/tracey/linker.h.
 */
bool tracey_link_elf_aarch64(const char *const *objects, size_t object_count, const char *output)
{
    struct link_object *loaded = NULL;
    uint64_t *bases = NULL;
    struct link_symbol_table table;
    struct link_buffer text;
    struct link_buffer strtab;
    struct link_buffer shstrtab;
    struct link_output_symbol *output_symbols = NULL;
    size_t output_symbol_count = 0;
    size_t output_symbol_capacity = 0;
    struct tracey_elf_sym *symbol_table = NULL;
    const unsigned char *debug_line = NULL;
    size_t debug_line_size = 0;
    const unsigned char *debug_info = NULL;
    size_t debug_info_size = 0;
    const unsigned char *debug_abbrev = NULL;
    size_t debug_abbrev_size = 0;
    bool needs_startup;
    uint64_t entry;
    bool ok = false;
    size_t i;

    if (output == NULL)
    {
        link_diagnostic("no output file specified\n");
        return false;
    }
    if (objects == NULL && object_count != 0)
    {
        link_diagnostic("no input objects specified\n");
        return false;
    }

    buffer_init(&text);
    buffer_init(&strtab);
    buffer_init(&shstrtab);
    link_symbol_table_init(&table);

    /*
     * Parse every input object. The objects own their text, symbol names and
     * relocation names; all of it is released in the cleanup block below.
     */
    loaded = (struct link_object *)calloc(object_count ? object_count : 1, sizeof(*loaded));
    if (loaded == NULL)
    {
        link_diagnostic("out of memory\n");
        goto done;
    }
    for (i = 0; i < object_count; i++)
    {
        if (objects[i] == NULL)
        {
            link_diagnostic("input object %zu has no path\n", i);
            goto done;
        }
        if (!link_object_read(objects[i], &loaded[i]))
            goto done;
    }

    /*
     * Concatenate the code sections. Each object starts on a four-byte
     * instruction boundary so that a section whose size is not a multiple of
     * four cannot misalign the following object. bases[i] records the final
     * offset of object i inside the combined .text.
     */
    bases = (uint64_t *)calloc(object_count ? object_count : 1, sizeof(*bases));
    if (bases == NULL)
    {
        link_diagnostic("out of memory\n");
        goto done;
    }
    for (i = 0; i < object_count; i++)
    {
        size_t padding = (size_t)(link_align_up(text.size, LINK_INSN_ALIGNMENT) - text.size);

        if (!buffer_append_zeros(&text, padding))
        {
            link_diagnostic("out of memory\n");
            goto done;
        }
        bases[i] = text.size;
        if (!buffer_append(&text, loaded[i].text, loaded[i].text_size))
        {
            link_diagnostic("out of memory\n");
            goto done;
        }
    }

    /*
     * Build the link-wide symbol table. Two passes are required: defined
     * global symbols are registered first so that a global definition wins
     * over a same-named local label regardless of the object order, then the
     * remaining defined names (local labels included) are added.
     */
    for (i = 0; i < object_count; i++)
    {
        size_t s;

        for (s = 0; s < loaded[i].symbol_count; s++)
        {
            const struct link_symbol *symbol = &loaded[i].symbols[s];

            if (!symbol->is_defined || !symbol->is_global || symbol->name[0] == '\0')
                continue;
            if (!link_symbol_table_add(&table, symbol->name,
                                       LINK_CODE_BASE_VA + bases[i] + symbol->value, true))
            {
                link_diagnostic("out of memory\n");
                goto done;
            }
        }
    }
    for (i = 0; i < object_count; i++)
    {
        size_t s;

        for (s = 0; s < loaded[i].symbol_count; s++)
        {
            const struct link_symbol *symbol = &loaded[i].symbols[s];

            if (!symbol->is_defined || symbol->name[0] == '\0')
                continue;
            if (!link_symbol_table_add(&table, symbol->name,
                                       LINK_CODE_BASE_VA + bases[i] + symbol->value,
                                       symbol->is_global))
            {
                link_diagnostic("out of memory\n");
                goto done;
            }
        }
    }

    /*
     * Patch the recorded relocations. The relocation offsets are relative to
     * the object's own .text, so the object base is added first. Every target
     * must be a symbol known globally; a missing definition is a hard error
     * because emitting a zero displacement would produce a silently corrupt
     * image.
     */
    for (i = 0; i < object_count; i++)
    {
        size_t r;

        for (r = 0; r < loaded[i].reloc_count; r++)
        {
            const struct link_reloc *reloc = &loaded[i].relocs[r];
            size_t target_index = link_symbol_table_find(&table, reloc->symbol);
            uint64_t pos = bases[i] + reloc->offset;
            uint64_t pc;
            int64_t delta;
            uint32_t instruction;

            if (target_index == SIZE_MAX)
            {
                link_diagnostic("%s: unresolved symbol '%s'\n", objects[i], reloc->symbol);
                goto done;
            }
            if (pos > (uint64_t)text.size || (uint64_t)text.size - pos < 4)
            {
                link_diagnostic("%s: relocation at offset %" PRIu64 " is outside .text\n",
                                objects[i], reloc->offset);
                goto done;
            }
            instruction = load_u32(text.data + pos);
            pc = LINK_CODE_BASE_VA + pos;
            delta = (int64_t)table.items[target_index].address - (int64_t)pc;

            switch (reloc->type)
            {
            case LINK_R_AARCH64_JUMP26:
            case LINK_R_AARCH64_CALL26:
                if ((delta & 3) != 0)
                {
                    link_diagnostic("%s: relocation against '%s' is not four-byte aligned\n",
                                    objects[i], reloc->symbol);
                    goto done;
                }
                if (delta < LINK_JUMP26_MIN_DELTA || delta > LINK_JUMP26_MAX_DELTA)
                {
                    link_diagnostic("%s: relocation against '%s' is out of range for a "
                                    "26-bit branch (%" PRId64 " bytes)\n",
                                    objects[i], reloc->symbol, delta);
                    goto done;
                }
                instruction = (instruction & ~LINK_JUMP26_MASK) |
                              (((uint32_t)(delta >> 2)) & LINK_JUMP26_MASK);
                break;
            case LINK_R_AARCH64_CONDBR19:
                if ((delta & 3) != 0)
                {
                    link_diagnostic("%s: relocation against '%s' is not four-byte aligned\n",
                                    objects[i], reloc->symbol);
                    goto done;
                }
                if (delta < LINK_CONDBR19_MIN_DELTA || delta > LINK_CONDBR19_MAX_DELTA)
                {
                    link_diagnostic("%s: relocation against '%s' is out of range for a "
                                    "19-bit conditional branch (%" PRId64 " bytes)\n",
                                    objects[i], reloc->symbol, delta);
                    goto done;
                }
                instruction = (instruction & ~LINK_CONDBR19_FIELD) |
                              ((((uint32_t)(delta >> 2)) & LINK_CONDBR19_MASK)
                               << LINK_CONDBR19_SHIFT);
                break;
            default:
                /*
                 * Relocation types the assembler does not emit are left
                 * untouched, matching the previous linker, which only rewrote
                 * the instruction for the three handled types.
                 */
                continue;
            }
            store_u32(text.data + pos, instruction);
        }
    }

    /*
     * Keep the debug information of the first object that carries it. The
     * assembler emits the sections per compilation unit; the previous linker
     * kept one copy of each and so does this one. The bytes are borrowed from
     * the input objects, which outlive every use below.
     */
    for (i = 0; i < object_count; i++)
    {
        if (debug_line == NULL && loaded[i].debug_line_size > 0)
        {
            debug_line = loaded[i].debug_line;
            debug_line_size = loaded[i].debug_line_size;
        }
        if (debug_info == NULL && loaded[i].debug_info_size > 0)
        {
            debug_info = loaded[i].debug_info;
            debug_info_size = loaded[i].debug_info_size;
        }
        if (debug_abbrev == NULL && loaded[i].debug_abbrev_size > 0)
        {
            debug_abbrev = loaded[i].debug_abbrev;
            debug_abbrev_size = loaded[i].debug_abbrev_size;
        }
    }

    /*
     * Emit a startup stub when the inputs have no _start of their own. The
     * stub is placed in front of all code, so every symbol address moves up by
     * its size; the relocations above were already resolved against the
     * unpadded layout, which stays consistent because both sides shift by the
     * same amount.
     */
    needs_startup = (link_symbol_table_find(&table, "_start") == SIZE_MAX);
    if (needs_startup)
    {
        unsigned char stub[(size_t)LINK_STARTUP_STUB_SIZE];
        uint64_t main_address = LINK_CODE_BASE_VA + LINK_STARTUP_STUB_SIZE;
        size_t main_index;
        int64_t delta;

        for (i = 0; i < table.count; i++)
            table.items[i].address += LINK_STARTUP_STUB_SIZE;

        main_index = link_symbol_table_find(&table, "main");
        if (main_index != SIZE_MAX)
            main_address = table.items[main_index].address;

        delta = (int64_t)main_address - (int64_t)LINK_CODE_BASE_VA;
        if ((delta & 3) != 0 || delta < LINK_JUMP26_MIN_DELTA || delta > LINK_JUMP26_MAX_DELTA)
        {
            link_diagnostic("startup stub cannot branch to 'main' (offset %" PRId64
                            " bytes); the program is too large\n",
                            delta);
            goto done;
        }

        store_u32(stub, LINK_BL_OPCODE | (((uint32_t)(delta >> 2)) & LINK_JUMP26_MASK));
        store_u32(stub + 4, LINK_STARTUP_MOV_X8);
        store_u32(stub + 8, LINK_STARTUP_SVC0);

        if (!buffer_reserve(&text, (size_t)LINK_STARTUP_STUB_SIZE))
        {
            link_diagnostic("out of memory\n");
            goto done;
        }
        if (text.size > 0)
            memmove(text.data + LINK_STARTUP_STUB_SIZE, text.data, text.size);
        memcpy(text.data, stub, sizeof(stub));
        text.size += (size_t)LINK_STARTUP_STUB_SIZE;
    }

    /* Entry point: the stub, or the program's own _start/main. */
    entry = LINK_CODE_BASE_VA;
    if (!needs_startup)
    {
        size_t start_index = link_symbol_table_find(&table, "_start");
        size_t main_index = link_symbol_table_find(&table, "main");

        if (start_index != SIZE_MAX)
            entry = table.items[start_index].address;
        else if (main_index != SIZE_MAX)
            entry = table.items[main_index].address;
    }

    /*
     * Select the symbols that go into the output .symtab: defined global
     * symbols with a real name, excluding compiler-generated ".L" labels, as
     * the previous implementation did. The list is sorted by name so the table
     * is deterministic and follows the same order std::map produced.
     */
    for (i = 0; i < table.count; i++)
    {
        const struct link_resolved_symbol *symbol = &table.items[i];
        struct link_output_symbol entry_symbol;

        if (symbol->name[0] == '\0')
            continue;
        if (symbol->name[0] == '.' && symbol->name[1] == 'L')
            continue;
        if (!symbol->is_global)
            continue;

        if (!grow_array((void **)&output_symbols, &output_symbol_capacity,
                        output_symbol_count + 1, sizeof(*output_symbols)))
        {
            link_diagnostic("out of memory\n");
            goto done;
        }
        entry_symbol.name = symbol->name;
        entry_symbol.address = symbol->address;
        entry_symbol.is_global = symbol->is_global;
        output_symbols[output_symbol_count++] = entry_symbol;
    }
    if (output_symbol_count > 1)
    {
        qsort(output_symbols, output_symbol_count, sizeof(*output_symbols),
              link_output_symbol_compare);
    }
    if (needs_startup)
    {
        struct link_output_symbol start_symbol;

        if (!grow_array((void **)&output_symbols, &output_symbol_capacity,
                        output_symbol_count + 1, sizeof(*output_symbols)))
        {
            link_diagnostic("out of memory\n");
            goto done;
        }
        start_symbol.name = "_start";
        start_symbol.address = LINK_CODE_BASE_VA;
        start_symbol.is_global = true;
        output_symbols[output_symbol_count++] = start_symbol;
    }

    /*
     * Build the symbol string table and the symbol table entries. Index 0 is
     * the reserved empty name, and the first entry is the null symbol required
     * by the ELF specification.
     */
    if (!buffer_append(&strtab, "", 1))
    {
        link_diagnostic("out of memory\n");
        goto done;
    }
    symbol_table = (struct tracey_elf_sym *)calloc(output_symbol_count + 1,
                                                   sizeof(*symbol_table));
    if (symbol_table == NULL)
    {
        link_diagnostic("out of memory\n");
        goto done;
    }
    for (i = 0; i < output_symbol_count; i++)
    {
        struct tracey_elf_sym *symbol = &symbol_table[i + 1];
        uint32_t name_offset;

        if (strtab.size > (size_t)UINT32_MAX)
        {
            link_diagnostic("symbol string table is too large\n");
            goto done;
        }
        name_offset = (uint32_t)strtab.size;
        if (!buffer_append(&strtab, output_symbols[i].name, strlen(output_symbols[i].name) + 1))
        {
            link_diagnostic("out of memory\n");
            goto done;
        }

        symbol->st_name = name_offset;
        symbol->st_info = (unsigned char)((output_symbols[i].is_global ? TRACEY_ELF_STB_GLOBAL
                                                                       : TRACEY_ELF_STB_LOCAL)
                                          << 4) |
                          TRACEY_ELF_STT_FUNC;
        symbol->st_other = 0;
        symbol->st_shndx = (uint16_t)LINK_INDEX_TEXT;
        symbol->st_value = output_symbols[i].address;
        symbol->st_size = 0;
    }

    /*
     * Build the section name string table. Entries are appended in the order
     * the section headers are written, and the recorded offsets are used for
     * the sh_name fields below. The debug section names are only present when
     * the corresponding section is emitted, so each gets its own offset.
     */
    {
        uint32_t offset_text = 0;
        uint32_t offset_symtab = 0;
        uint32_t offset_strtab = 0;
        uint32_t offset_shstrtab = 0;
        uint32_t offset_debug_line = 0;
        uint32_t offset_debug_info = 0;
        uint32_t offset_debug_abbrev = 0;
        struct link_named_section
        {
            const char *name;
            uint32_t *p_offset;
            bool enabled;
        } names[7];
        size_t name_count = 0;
        size_t n;

        names[name_count].name = ".text";
        names[name_count].p_offset = &offset_text;
        names[name_count].enabled = true;
        name_count++;
        names[name_count].name = ".symtab";
        names[name_count].p_offset = &offset_symtab;
        names[name_count].enabled = true;
        name_count++;
        names[name_count].name = ".strtab";
        names[name_count].p_offset = &offset_strtab;
        names[name_count].enabled = true;
        name_count++;
        names[name_count].name = ".shstrtab";
        names[name_count].p_offset = &offset_shstrtab;
        names[name_count].enabled = true;
        name_count++;
        names[name_count].name = ".debug_line";
        names[name_count].p_offset = &offset_debug_line;
        names[name_count].enabled = debug_line_size > 0;
        name_count++;
        names[name_count].name = ".debug_info";
        names[name_count].p_offset = &offset_debug_info;
        names[name_count].enabled = debug_info_size > 0;
        name_count++;
        names[name_count].name = ".debug_abbrev";
        names[name_count].p_offset = &offset_debug_abbrev;
        names[name_count].enabled = debug_abbrev_size > 0;
        name_count++;

        if (!buffer_append(&shstrtab, "", 1))
        {
            link_diagnostic("out of memory\n");
            goto done;
        }
        for (n = 0; n < name_count; n++)
        {
            if (!names[n].enabled)
                continue;
            if (shstrtab.size > (size_t)UINT32_MAX)
            {
                link_diagnostic("section string table is too large\n");
                goto done;
            }
            *names[n].p_offset = (uint32_t)shstrtab.size;
            if (!buffer_append(&shstrtab, names[n].name, strlen(names[n].name) + 1))
            {
                link_diagnostic("out of memory\n");
                goto done;
            }
        }

        /*
         * Compute the file layout. Every variable-sized section starts on an
         * eight-byte boundary; the code itself already sits on its own
         * instruction-aligned offset inside the loadable segment.
         */
        {
            uint64_t symtab_size = (uint64_t)(output_symbol_count + 1) * LINK_SYMBOL_ENTRY_SIZE;
            uint64_t offset = LINK_TEXT_FILE_OFFSET + (uint64_t)text.size;
            uint64_t symtab_offset;
            uint64_t strtab_offset;
            uint64_t shstrtab_offset;
            uint64_t debug_line_offset;
            uint64_t debug_info_offset;
            uint64_t debug_abbrev_offset;
            uint64_t shdr_offset;
            uint64_t section_count = 5;
            FILE *file = NULL;

            offset = link_align_up(offset, LINK_SECTION_ALIGNMENT);
            symtab_offset = offset;
            offset += symtab_size;
            offset = link_align_up(offset, LINK_SECTION_ALIGNMENT);
            strtab_offset = offset;
            offset += (uint64_t)strtab.size;
            offset = link_align_up(offset, LINK_SECTION_ALIGNMENT);
            shstrtab_offset = offset;
            offset += (uint64_t)shstrtab.size;
            offset = link_align_up(offset, LINK_SECTION_ALIGNMENT);
            debug_line_offset = offset;
            if (debug_line_size > 0)
                offset += (uint64_t)debug_line_size;
            offset = link_align_up(offset, LINK_SECTION_ALIGNMENT);
            debug_info_offset = offset;
            if (debug_info_size > 0)
                offset += (uint64_t)debug_info_size;
            offset = link_align_up(offset, LINK_SECTION_ALIGNMENT);
            debug_abbrev_offset = offset;
            if (debug_abbrev_size > 0)
                offset += (uint64_t)debug_abbrev_size;
            offset = link_align_up(offset, LINK_SECTION_ALIGNMENT);
            shdr_offset = offset;

            if (debug_line_size > 0)
                section_count++;
            if (debug_info_size > 0)
                section_count++;
            if (debug_abbrev_size > 0)
                section_count++;

            file = fopen(output, "wb");
            if (file == NULL)
            {
                link_diagnostic("cannot open '%s' for writing\n", output);
                goto done;
            }

            {
                struct tracey_elf_ehdr ehdr;
                struct tracey_elf_phdr phdr;
                uint64_t cursor;

                memset(&ehdr, 0, sizeof(ehdr));
                ehdr.e_ident[0] = 0x7f;
                ehdr.e_ident[1] = 'E';
                ehdr.e_ident[2] = 'L';
                ehdr.e_ident[3] = 'F';
                ehdr.e_ident[4] = 2; /* ELFCLASS64 */
                ehdr.e_ident[5] = 1; /* ELFDATA2LSB */
                ehdr.e_ident[6] = 1; /* EV_CURRENT */
                ehdr.e_type = TRACEY_ELF_ET_EXEC;
                ehdr.e_machine = TRACEY_ELF_EM_AARCH64;
                ehdr.e_version = 1;
                ehdr.e_entry = entry;
                ehdr.e_phoff = LINK_EHDR_SIZE;
                ehdr.e_shoff = shdr_offset;
                ehdr.e_ehsize = (uint16_t)LINK_EHDR_SIZE;
                ehdr.e_phentsize = (uint16_t)LINK_PHDR_SIZE;
                ehdr.e_phnum = 1;
                ehdr.e_shentsize = (uint16_t)LINK_SECTION_HEADER_SIZE;
                ehdr.e_shnum = (uint16_t)section_count;
                ehdr.e_shstrndx = (uint16_t)LINK_INDEX_SHSTRTAB;

                memset(&phdr, 0, sizeof(phdr));
                phdr.p_type = TRACEY_ELF_PT_LOAD;
                phdr.p_flags = TRACEY_ELF_PF_R | TRACEY_ELF_PF_X;
                phdr.p_offset = 0;
                phdr.p_vaddr = LINK_BASE_VA;
                phdr.p_paddr = LINK_BASE_VA;
                phdr.p_filesz = LINK_TEXT_FILE_OFFSET + (uint64_t)text.size;
                phdr.p_memsz = phdr.p_filesz;
                phdr.p_align = LINK_PROGRAM_ALIGNMENT;

                tracey_elf_write_ehdr(file, &ehdr);
                tracey_elf_write_phdr(file, &phdr);
                cursor = LINK_EHDR_SIZE + LINK_PHDR_SIZE;

                if (!link_write_padding(file, cursor, LINK_TEXT_FILE_OFFSET))
                {
                    link_diagnostic("cannot write '%s'\n", output);
                    fclose(file);
                    goto done;
                }
                if (text.size > 0 &&
                    fwrite(text.data, 1, text.size, file) != text.size)
                {
                    link_diagnostic("cannot write '%s'\n", output);
                    fclose(file);
                    goto done;
                }
                cursor = LINK_TEXT_FILE_OFFSET + (uint64_t)text.size;

                if (!link_write_padding(file, cursor, symtab_offset))
                {
                    link_diagnostic("cannot write '%s'\n", output);
                    fclose(file);
                    goto done;
                }
                for (i = 0; i <= output_symbol_count; i++)
                    tracey_elf_write_sym(file, &symbol_table[i]);
                cursor = symtab_offset + symtab_size;

                if (!link_write_padding(file, cursor, strtab_offset) ||
                    !link_write_bytes(file, strtab.data, strtab.size))
                {
                    link_diagnostic("cannot write '%s'\n", output);
                    fclose(file);
                    goto done;
                }
                cursor = strtab_offset + (uint64_t)strtab.size;

                if (!link_write_padding(file, cursor, shstrtab_offset) ||
                    !link_write_bytes(file, shstrtab.data, shstrtab.size))
                {
                    link_diagnostic("cannot write '%s'\n", output);
                    fclose(file);
                    goto done;
                }
                cursor = shstrtab_offset + (uint64_t)shstrtab.size;

                if (debug_line_size > 0)
                {
                    if (!link_write_padding(file, cursor, debug_line_offset) ||
                        !link_write_bytes(file, debug_line, debug_line_size))
                    {
                        link_diagnostic("cannot write '%s'\n", output);
                        fclose(file);
                        goto done;
                    }
                    cursor = debug_line_offset + (uint64_t)debug_line_size;
                }
                if (debug_info_size > 0)
                {
                    if (!link_write_padding(file, cursor, debug_info_offset) ||
                        !link_write_bytes(file, debug_info, debug_info_size))
                    {
                        link_diagnostic("cannot write '%s'\n", output);
                        fclose(file);
                        goto done;
                    }
                    cursor = debug_info_offset + (uint64_t)debug_info_size;
                }
                if (debug_abbrev_size > 0)
                {
                    if (!link_write_padding(file, cursor, debug_abbrev_offset) ||
                        !link_write_bytes(file, debug_abbrev, debug_abbrev_size))
                    {
                        link_diagnostic("cannot write '%s'\n", output);
                        fclose(file);
                        goto done;
                    }
                    cursor = debug_abbrev_offset + (uint64_t)debug_abbrev_size;
                }

                if (!link_write_padding(file, cursor, shdr_offset))
                {
                    link_diagnostic("cannot write '%s'\n", output);
                    fclose(file);
                    goto done;
                }

                /* Section 0: the null header required by ELF. */
                {
                    struct tracey_elf_shdr shdr;

                    memset(&shdr, 0, sizeof(shdr));
                    tracey_elf_write_shdr(file, &shdr);
                }

                /* Section 1: .text, part of the loadable segment. */
                {
                    struct tracey_elf_shdr shdr;

                    memset(&shdr, 0, sizeof(shdr));
                    shdr.sh_name = offset_text;
                    shdr.sh_type = TRACEY_ELF_SHT_PROGBITS;
                    shdr.sh_flags = TRACEY_ELF_SHF_ALLOC | TRACEY_ELF_SHF_EXECINSTR;
                    shdr.sh_addr = LINK_CODE_BASE_VA;
                    shdr.sh_offset = LINK_TEXT_FILE_OFFSET;
                    shdr.sh_size = (uint64_t)text.size;
                    shdr.sh_addralign = LINK_TEXT_SHADDR_ALIGNMENT;
                    tracey_elf_write_shdr(file, &shdr);
                }

                /* Section 2: .symtab, linked to .strtab. */
                {
                    struct tracey_elf_shdr shdr;

                    memset(&shdr, 0, sizeof(shdr));
                    shdr.sh_name = offset_symtab;
                    shdr.sh_type = TRACEY_ELF_SHT_SYMTAB;
                    shdr.sh_offset = symtab_offset;
                    shdr.sh_size = symtab_size;
                    shdr.sh_link = LINK_INDEX_STRTAB;
                    shdr.sh_info = 1;
                    shdr.sh_addralign = LINK_SECTION_ALIGNMENT;
                    shdr.sh_entsize = LINK_SYMBOL_ENTRY_SIZE;
                    tracey_elf_write_shdr(file, &shdr);
                }

                /* Section 3: .strtab. */
                {
                    struct tracey_elf_shdr shdr;

                    memset(&shdr, 0, sizeof(shdr));
                    shdr.sh_name = offset_strtab;
                    shdr.sh_type = TRACEY_ELF_SHT_STRTAB;
                    shdr.sh_offset = strtab_offset;
                    shdr.sh_size = (uint64_t)strtab.size;
                    shdr.sh_addralign = 1;
                    tracey_elf_write_shdr(file, &shdr);
                }

                /* Section 4: .shstrtab. */
                {
                    struct tracey_elf_shdr shdr;

                    memset(&shdr, 0, sizeof(shdr));
                    shdr.sh_name = offset_shstrtab;
                    shdr.sh_type = TRACEY_ELF_SHT_STRTAB;
                    shdr.sh_offset = shstrtab_offset;
                    shdr.sh_size = (uint64_t)shstrtab.size;
                    shdr.sh_addralign = 1;
                    tracey_elf_write_shdr(file, &shdr);
                }

                /* Optional debug sections, emitted in a fixed order. */
                if (debug_line_size > 0)
                {
                    struct tracey_elf_shdr shdr;

                    memset(&shdr, 0, sizeof(shdr));
                    shdr.sh_name = offset_debug_line;
                    shdr.sh_type = TRACEY_ELF_SHT_PROGBITS;
                    shdr.sh_offset = debug_line_offset;
                    shdr.sh_size = (uint64_t)debug_line_size;
                    shdr.sh_addralign = 1;
                    tracey_elf_write_shdr(file, &shdr);
                }
                if (debug_info_size > 0)
                {
                    struct tracey_elf_shdr shdr;

                    memset(&shdr, 0, sizeof(shdr));
                    shdr.sh_name = offset_debug_info;
                    shdr.sh_type = TRACEY_ELF_SHT_PROGBITS;
                    shdr.sh_offset = debug_info_offset;
                    shdr.sh_size = (uint64_t)debug_info_size;
                    shdr.sh_addralign = 1;
                    tracey_elf_write_shdr(file, &shdr);
                }
                if (debug_abbrev_size > 0)
                {
                    struct tracey_elf_shdr shdr;

                    memset(&shdr, 0, sizeof(shdr));
                    shdr.sh_name = offset_debug_abbrev;
                    shdr.sh_type = TRACEY_ELF_SHT_PROGBITS;
                    shdr.sh_offset = debug_abbrev_offset;
                    shdr.sh_size = (uint64_t)debug_abbrev_size;
                    shdr.sh_addralign = 1;
                    tracey_elf_write_shdr(file, &shdr);
                }

                if (ferror(file))
                {
                    link_diagnostic("cannot write '%s'\n", output);
                    fclose(file);
                    goto done;
                }
                if (fclose(file) != 0)
                {
                    link_diagnostic("cannot finish writing '%s'\n", output);
                    goto done;
                }
            }
        }
    }

    ok = true;

done:
    free(symbol_table);
    free(output_symbols);
    link_symbol_table_free(&table);
    buffer_free(&shstrtab);
    buffer_free(&strtab);
    buffer_free(&text);
    if (loaded != NULL)
    {
        for (i = 0; i < object_count; i++)
            link_object_free(&loaded[i]);
        free(loaded);
    }
    free(bases);
    return ok;
}
