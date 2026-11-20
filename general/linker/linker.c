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

#include "linker_internal.h"

#include <inttypes.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*
 * link_diagnostic - report a linker failure on stderr.
 *
 * fmt and the variadic arguments follow printf conventions. Every message is
 * prefixed with the toolchain identifier so linker failures can be told apart
 * from assembler or code generator messages. No trailing newline is added;
 * callers terminate their format strings with "\n".
 */
void link_diagnostic(const char *fmt, ...)
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
uint64_t link_align_up(uint64_t value, uint64_t alignment)
{
    uint64_t remainder = value % alignment;

    return remainder == 0 ? value : value + (alignment - remainder);
}

/*
 * buffer_init - place a buffer in the empty state.
 */
void buffer_init(struct link_buffer *buffer)
{
    buffer->data = NULL;
    buffer->size = 0;
    buffer->capacity = 0;
}

/*
 * buffer_free - release the storage of a buffer and reset it.
 */
void buffer_free(struct link_buffer *buffer)
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
bool grow_array(void **p_items, size_t *p_capacity, size_t needed, size_t item_size)
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
bool buffer_reserve(struct link_buffer *buffer, size_t extra)
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
bool buffer_append(struct link_buffer *buffer, const void *data, size_t size)
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
bool buffer_append_zeros(struct link_buffer *buffer, size_t count)
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
bool link_read_file(const char *path, struct link_buffer *out)
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
void reader_init(struct link_reader *reader, const unsigned char *data, size_t size)
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
bool reader_range_ok(const struct link_reader *reader, uint64_t offset, uint64_t length)
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
bool reader_u16(const struct link_reader *reader, uint64_t offset, uint16_t *out)
{
    if (!reader_range_ok(reader, offset, 2))
        return false;
    *out = (uint16_t)((uint16_t)reader->data[offset] |
                      (uint16_t)((uint16_t)reader->data[offset + 1] << 8));
    return true;
}

bool reader_u32(const struct link_reader *reader, uint64_t offset, uint32_t *out)
{
    if (!reader_range_ok(reader, offset, 4))
        return false;
    *out = (uint32_t)reader->data[offset] | ((uint32_t)reader->data[offset + 1] << 8) |
           ((uint32_t)reader->data[offset + 2] << 16) | ((uint32_t)reader->data[offset + 3] << 24);
    return true;
}

bool reader_u64(const struct link_reader *reader, uint64_t offset, uint64_t *out)
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
void store_u32(unsigned char *p, uint32_t value)
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
uint32_t load_u32(const unsigned char *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

/*
 * link_strdup - duplicate a NUL-terminated string on the heap.
 *
 * Returns NULL when the allocation fails. The caller owns the result and
 * releases it with free().
 */
char *link_strdup(const char *text)
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
bool section_from_header(const struct link_reader *reader,
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
bool section_string(const struct link_section *table, uint32_t index, const char **out)
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
bool section_name_is(const struct link_section *shstrtab, uint32_t name_index,
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
void link_object_free(struct link_object *object)
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
bool link_object_take_debug(unsigned char **p_data, size_t *p_size,
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
bool tracey_link_elf_aarch64(const char *const *objects, size_t object_count, const char *output)
{
    struct link_object *loaded = NULL;
    uint64_t *bases = NULL;
    struct link_symbol_table table;
    struct link_buffer text;
    const unsigned char *debug_line = NULL;
    size_t debug_line_size = 0;
    const unsigned char *debug_info = NULL;
    size_t debug_info_size = 0;
    const unsigned char *debug_abbrev = NULL;
    size_t debug_abbrev_size = 0;
    bool needs_startup;
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
    link_symbol_table_init(&table);

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

    bases = (uint64_t *)calloc(object_count ? object_count : 1, sizeof(*bases));
    if (bases == NULL)
    {
        link_diagnostic("out of memory\n");
        goto done;
    }
    if (!link_concatenate_text(loaded, object_count, bases, &text))
        goto done;
    if (!link_build_symbol_table(loaded, object_count, bases, &table))
        goto done;
    if (!link_patch_relocations(loaded, object_count, objects, bases, &table, &text))
        goto done;

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

    needs_startup = (link_symbol_table_find(&table, "_start") == SIZE_MAX);
    if (needs_startup && !link_emit_startup_stub(&table, &text))
        goto done;

    if (!link_write_output(output, &text, &table, debug_line, debug_line_size,
                          debug_info, debug_info_size, debug_abbrev, debug_abbrev_size))
        goto done;

    ok = true;

done:
    link_symbol_table_free(&table);
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

bool link_concatenate_text(struct link_object *loaded, size_t object_count,
                          uint64_t *bases, struct link_buffer *text)
{
    size_t i;

    for (i = 0; i < object_count; i++)
    {
        size_t padding = (size_t)(link_align_up(text->size, LINK_INSN_ALIGNMENT) - text->size);

        if (!buffer_append_zeros(text, padding))
        {
            link_diagnostic("out of memory\n");
            return false;
        }
        bases[i] = text->size;
        if (!buffer_append(text, loaded[i].text, loaded[i].text_size))
        {
            link_diagnostic("out of memory\n");
            return false;
        }
    }
    return true;
}


bool link_build_symbol_table(struct link_object *loaded, size_t object_count,
                            const uint64_t *bases, struct link_symbol_table *table)
{
    size_t i;

    for (i = 0; i < object_count; i++)
    {
        size_t s;

        for (s = 0; s < loaded[i].symbol_count; s++)
        {
            const struct link_symbol *symbol = &loaded[i].symbols[s];

            if (!symbol->is_defined || !symbol->is_global || symbol->name[0] == '\0')
                continue;
            if (!link_symbol_table_add(table, symbol->name,
                                       LINK_CODE_BASE_VA + bases[i] + symbol->value, true))
            {
                link_diagnostic("out of memory\n");
                return false;
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
            if (!link_symbol_table_add(table, symbol->name,
                                       LINK_CODE_BASE_VA + bases[i] + symbol->value,
                                       symbol->is_global))
            {
                link_diagnostic("out of memory\n");
                return false;
            }
        }
    }
    return true;
}


