/*
 * AArch64 assembler.
 *
 * This module belongs to the Tracey bootstrap toolchain and translates the
 * restricted assembly dialect produced by the code generator into machine
 * code, DWARF4 line information and ELF64 relocation metadata.
 *
 * The accepted dialect is deliberately small:
 *
 *   - one directive or instruction per line;
 *   - a line whose last non-blank character is ':' defines a label;
 *   - .file, .loc, .globl, .text, .data and .asciz are recognized, every
 *     other directive is ignored.
 *
 * There is no per-section bookkeeping: .text, .data, .rodata and .bss all
 * contribute to one flat output blob. Label offsets, relocation offsets and
 * symbol values are therefore byte offsets into that single blob, exactly as
 * in the assembler this implementation replaces.
 *
 * Encoding errors are recoverable in the sense that the assembler returns a
 * result structure with ok == false and a diagnostic instead of aborting.
 * The assembler does not attempt to resynchronize or continue after an
 * encoding error.
 */

#include "tracey/assembler.h"

#include "tracey/elf.h"

#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Value used in place of std::string::npos. */

/*
 * Base virtual address assumed for the .text blob when DWARF addresses are
 * generated. The assembler produces relocatable objects, so this address is
 * only used to fill in .debug_line and .debug_info; the linker relocates the
 * final program independently.
 */

/*
 * Maximum number of comma separated operands inspected by the encoder. The
 * widest supported instruction takes four operands; further operands are
 * counted but not stored, which keeps the arity checks below meaningful
 * without allocating per-instruction storage.
 */

/*
 * R_AARCH64_CONDBR19 is not part of the shared ELF constants header, so the
 * number is kept local. R_AARCH64_JUMP26 and R_AARCH64_CALL26 come from
 * tracey/elf.h.
 */

/*
 * A non-owning view into a character buffer.
 *
 * p points at the first byte and n is the number of bytes. Views are never
 * NUL-terminated unless they happen to cover a complete line of the source
 * copy, so they must not be passed to string functions that require a
 * terminator.
 */
#include "assembler_internal.h"

struct tracey_asm_result tracey_assemble_aarch64(const char *source, size_t source_len)
{
    struct tracey_asm_result res;
    char *src = NULL;
    struct asm_vec lines;
    struct asm_vec files;
    struct asm_vec labels;
    struct asm_vec globals;
    struct asm_vec entries;
    struct asm_vec symbols;
    struct asm_vec relocs;
    struct asm_buf text;
    struct asm_buf dbg;
    struct asm_buf abbrev;
    struct asm_buf info;
    struct asm_emit emit;
    struct asm_slice *line_items;
    struct asm_label **sorted_labels = NULL;
    size_t line_count;
    size_t unit_len_pos;
    size_t header_len_pos;
    size_t header_end;
    size_t ulen_pos;
    size_t off = 0;
    size_t i;
    size_t j;
    int cur_file = 1;
    int cur_line = 1;
    int file_index = 1;
    enum asm_encode_status status;

    memset(&res, 0, sizeof res);
    asm_vec_init(&lines, sizeof(struct asm_slice));
    asm_vec_init(&files, sizeof(char *));
    asm_vec_init(&labels, sizeof(struct asm_label));
    asm_vec_init(&globals, sizeof(char *));
    asm_vec_init(&entries, sizeof(struct asm_line_entry));
    asm_vec_init(&symbols, sizeof(struct tracey_asm_symbol));
    asm_vec_init(&relocs, sizeof(struct tracey_asm_reloc));
    asm_buf_init(&text);
    asm_buf_init(&dbg);
    asm_buf_init(&abbrev);
    asm_buf_init(&info);

    if (source == NULL)
        source_len = 0;

    /*
     * Copy the source and split it the way std::getline() did: a trailing
     * newline does not produce an extra empty line, and an empty buffer
     * produces no lines at all.
     */
    src = malloc(source_len + 1);
    if (src == NULL)
        goto oom;
    if (source_len > 0)
        memcpy(src, source, source_len);
    src[source_len] = '\0';
    {
        size_t start = 0;

        while (start < source_len)
        {
            size_t end = start;
            struct asm_slice *slot;

            while (end < source_len && src[end] != '\n')
                end++;
            slot = asm_vec_push(&lines);
            if (slot == NULL)
                goto oom;
            *slot = asm_make_slice(src + start, end - start);
            if (end >= source_len)
                break;
            start = end + 1;
        }
    }
    line_items = lines.items;
    line_count = lines.len;

    /* First pass: label offsets and the DWARF file table. */
    for (i = 0; i < line_count; i++)
    {
        struct asm_slice l = asm_slice_trim(line_items[i]);

        if (l.n == 0)
            continue;
        if (l.p[l.n - 1] == ':')
        {
            if (!asm_label_set(&labels, asm_slice_sub(l, 0, l.n - 1), off))
                goto oom;
            continue;
        }
        if (l.p[0] == '.')
        {
            if (asm_slice_starts(l, ".globl"))
                continue;
            if (asm_slice_eq(l, ".text"))
                continue;
            if (asm_slice_eq(l, ".data"))
                continue;
            if (asm_slice_starts(l, ".file"))
            {
                if (!asm_file_index(&files,
                                    asm_quoted_payload(l, asm_slice_cstr("unknown")),
                                    &file_index))
                    goto oom;
                continue;
            }
            if (asm_slice_starts(l, ".loc"))
                continue;
            if (asm_slice_starts(l, ".asciz"))
            {
                struct asm_slice payload = asm_quoted_payload(l, asm_slice_cstr(""));

                off += asm_escaped_length(payload) + 1;
                continue;
            }
            /* Unknown directives contribute no bytes. */
            continue;
        }
        off += 4;
    }

    if (files.len == 0)
    {
        if (!asm_file_index(&files, asm_slice_cstr("unknown"), &file_index))
            goto oom;
    }

    /* Second pass: encode instructions and record line table entries. */
    emit.p_text = &text;
    emit.p_relocs = &relocs;
    emit.p_labels = &labels;
    emit.p_result = &res;
    emit.off = 0;
    cur_file = 1;
    cur_line = 1;

    for (i = 0; i < line_count; i++)
    {
        struct asm_slice l = asm_slice_trim(line_items[i]);
        struct asm_slice mnemonic;
        struct asm_slice operands;
        char mnemonic_buf[64];
        size_t sp;
        size_t mn;
        size_t mi;

        if (l.n == 0)
            continue;
        if (l.p[l.n - 1] == ':')
            continue;
        if (l.p[0] == '.')
        {
            if (asm_slice_starts(l, ".file"))
            {
                if (!asm_file_index(&files,
                                    asm_quoted_payload(l, asm_slice_cstr("unknown")),
                                    &cur_file))
                    goto oom;
                continue;
            }
            if (asm_slice_starts(l, ".loc"))
            {
                int line_number;

                /* .loc <line> [<column>] [<options>]: only the line number
                   is consumed, as in the original stream extraction. */
                if (asm_parse_int(asm_slice_sub(l, 4, ASM_NPOS), &line_number))
                    cur_line = line_number;
                continue;
            }
            if (asm_slice_starts(l, ".globl"))
            {
                char **slot = asm_vec_push(&globals);

                if (slot == NULL)
                    goto oom;
                *slot = asm_slice_dup(asm_slice_trim(asm_slice_sub(l, 7, ASM_NPOS)));
                if (*slot == NULL)
                    goto oom;
            }
            else if (asm_slice_eq(l, ".text") || asm_slice_eq(l, ".data"))
            {
                /*
                 * Sections share one output blob, so switching sections
                 * carries no state. .rodata and .bss are not special-cased
                 * and fall into the unknown-directive path below.
                 */
            }
            else if (asm_slice_starts(l, ".asciz"))
            {
                struct asm_slice payload = asm_quoted_payload(l, asm_slice_cstr(""));
                size_t k = 0;
                size_t written = 0;

                while (k < payload.n)
                {
                    if (payload.p[k] == '\\' && k + 1 < payload.n)
                    {
                        char c = payload.p[k + 1];

                        if (c == 'n')
                        {
                            asm_buf_byte(&text, '\n');
                            k += 2;
                            written++;
                            continue;
                        }
                        if (c == 't')
                        {
                            asm_buf_byte(&text, '\t');
                            k += 2;
                            written++;
                            continue;
                        }
                        if (c == 'r')
                        {
                            asm_buf_byte(&text, '\r');
                            k += 2;
                            written++;
                            continue;
                        }
                        if (c == '0')
                        {
                            asm_buf_byte(&text, 0);
                            k += 2;
                            written++;
                            continue;
                        }
                        if (c == '\\')
                        {
                            asm_buf_byte(&text, '\\');
                            k += 2;
                            written++;
                            continue;
                        }
                        if (c == '"')
                        {
                            asm_buf_byte(&text, '"');
                            k += 2;
                            written++;
                            continue;
                        }
                        /*
                         * Unknown escape: the backslash is emitted and the
                         * following character is re-examined normally.
                         */
                        asm_buf_byte(&text, (uint8_t)payload.p[k]);
                    }
                    else
                    {
                        asm_buf_byte(&text, (uint8_t)payload.p[k]);
                    }
                    k++;
                    written++;
                }
                asm_buf_byte(&text, 0);
                written++;
                emit.off += written;
            }
            continue;
        }

        if (cur_line > 0)
        {
            struct asm_line_entry *entry = asm_vec_push(&entries);

            if (entry == NULL)
                goto oom;
            entry->addr = (size_t)(ASM_BASE_ADDR + (uint64_t)emit.off);
            entry->file = cur_file;
            entry->line = cur_line;
        }

        sp = ASM_NPOS;
        for (mn = 0; mn < l.n; mn++)
        {
            if (l.p[mn] == ' ' || l.p[mn] == '\t')
            {
                sp = mn;
                break;
            }
        }
        if (sp == ASM_NPOS)
        {
            mnemonic = l;
            operands = asm_slice_cstr("");
        }
        else
        {
            mnemonic = asm_slice_sub(l, 0, sp);
            operands = asm_slice_trim(asm_slice_sub(l, sp, ASM_NPOS));
        }
        mn = mnemonic.n < sizeof mnemonic_buf - 1 ? mnemonic.n : sizeof mnemonic_buf - 1;
        for (mi = 0; mi < mn; mi++)
            mnemonic_buf[mi] = (char)tolower((unsigned char)mnemonic.p[mi]);
        mnemonic_buf[mn] = '\0';

        status = asm_encode_line(&emit, mnemonic_buf, operands);
        if (status == ASM_ENCODE_NOMEM)
            goto oom;
        if (status == ASM_ENCODE_ERROR)
            goto fail;
    }

    /*
     * Build the symbol table from the label table. Labels are ordered by
     * name because the original stored them in a std::map, and that order
     * determines the .symtab layout.
     */
    if (labels.len > 0)
    {
        sorted_labels = malloc(labels.len * sizeof *sorted_labels);
        if (sorted_labels == NULL)
            goto oom;
        for (i = 0; i < labels.len; i++)
            sorted_labels[i] = (struct asm_label *)labels.items + i;
        qsort(sorted_labels, labels.len, sizeof *sorted_labels, asm_compare_labels);
        for (i = 0; i < labels.len; i++)
        {
            struct tracey_asm_symbol *sym = asm_vec_push(&symbols);

            if (sym == NULL)
                goto oom;
            /*
             * The symbol table owns its own copy. The label entry keeps its
             * name because the later DWARF function lookup still searches
             * the label table; clearing it here made that lookup strcmp()
             * a NULL pointer.
             */
            sym->name = asm_slice_dup(asm_slice_cstr(sorted_labels[i]->name));
            if (sym->name == NULL)
                goto oom;
            sym->offset = sorted_labels[i]->offset;
            sym->global = false;
        }
    }

    for (i = 0; i < globals.len; i++)
    {
        const char *name = ((char **)globals.items)[i];

        for (j = 0; j < symbols.len; j++)
        {
            struct tracey_asm_symbol *sym = (struct tracey_asm_symbol *)symbols.items + j;

            if (strcmp(sym->name, name) == 0)
                sym->global = true;
        }
    }

    /* DWARF4 .debug_line. */
    unit_len_pos = dbg.len;
    asm_buf_u32(&dbg, 0); /* unit_length, patched below */
    asm_buf_u16(&dbg, 4); /* version */
    header_len_pos = dbg.len;
    asm_buf_u32(&dbg, 0);                 /* header_length, patched below */
    asm_buf_byte(&dbg, 1);                /* minimum_instruction_length */
    asm_buf_byte(&dbg, 1);                /* maximum_operations_per_instruction */
    asm_buf_byte(&dbg, 1);                /* default_is_stmt */
    asm_buf_byte(&dbg, (uint8_t)-5);      /* line_base (signed -5) */
    asm_buf_byte(&dbg, 14);               /* line_range */
    asm_buf_byte(&dbg, 13);               /* opcode_base */
    {
        const uint8_t std_opcode_lengths[12] = {0, 1, 1, 1, 1, 0, 0, 0, 1, 0, 0, 1};

        for (i = 0; i < 12; i++)
            asm_buf_byte(&dbg, std_opcode_lengths[i]);
    }
    asm_buf_byte(&dbg, 0); /* include_directories: empty list */
    for (i = 0; i < files.len; i++)
    {
        const char *name = ((char **)files.items)[i];

        asm_buf_bytes(&dbg, name, strlen(name));
        asm_buf_byte(&dbg, 0);
        asm_buf_uleb(&dbg, 0); /* directory index */
        asm_buf_uleb(&dbg, 0); /* modification time */
        asm_buf_uleb(&dbg, 0); /* file size */
    }
    asm_buf_byte(&dbg, 0); /* end of file list */
    header_end = dbg.len;
    asm_buf_patch_u32(&dbg, header_len_pos,
                      (uint32_t)(header_end - (header_len_pos + 4)));

    {
        size_t prev_addr = 0;
        int prev_line = 1;
        int prev_file = 1;
        struct asm_line_entry *items = entries.items;

        for (i = 0; i < entries.len; i++)
        {
            if (items[i].file != prev_file)
            {
                asm_buf_byte(&dbg, 4); /* DW_LNS_set_file */
                asm_buf_uleb(&dbg, (uint64_t)items[i].file);
                prev_file = items[i].file;
            }
            asm_buf_byte(&dbg, 3); /* DW_LNS_advance_line */
            asm_buf_sleb(&dbg, (int64_t)(items[i].line - prev_line));
            asm_buf_byte(&dbg, 2); /* DW_LNS_advance_pc */
            asm_buf_uleb(&dbg, (uint64_t)(items[i].addr - prev_addr));
            asm_buf_byte(&dbg, 1); /* DW_LNS_copy */
            prev_addr = items[i].addr;
            prev_line = items[i].line;
        }
    }
    asm_buf_byte(&dbg, 0);   /* DW_LNE_end_sequence */
    asm_buf_uleb(&dbg, 1);
    asm_buf_byte(&dbg, 1);
    asm_buf_patch_u32(&dbg, unit_len_pos, (uint32_t)(dbg.len - 4));

    /* .debug_abbrev: one compile unit and one subprogram declaration. */
    asm_buf_uleb(&abbrev, 1);
    asm_buf_byte(&abbrev, 0x11); /* DW_TAG_compile_unit */
    asm_buf_byte(&abbrev, 1);    /* DW_CHILDREN_yes */
    asm_buf_uleb(&abbrev, 0x03); /* DW_AT_name */
    asm_buf_uleb(&abbrev, 0x08); /* DW_FORM_string */
    asm_buf_uleb(&abbrev, 0x10); /* DW_AT_stmt_list */
    asm_buf_uleb(&abbrev, 0x17); /* DW_FORM_sec_offset */
    asm_buf_uleb(&abbrev, 0x11); /* DW_AT_low_pc */
    asm_buf_uleb(&abbrev, 0x01); /* DW_FORM_addr */
    asm_buf_uleb(&abbrev, 0x12); /* DW_AT_high_pc */
    asm_buf_uleb(&abbrev, 0x01); /* DW_FORM_addr */
    asm_buf_byte(&abbrev, 0);
    asm_buf_byte(&abbrev, 0);
    asm_buf_uleb(&abbrev, 2);
    asm_buf_byte(&abbrev, 0x2e); /* DW_TAG_subprogram */
    asm_buf_byte(&abbrev, 0);    /* DW_CHILDREN_no */
    asm_buf_uleb(&abbrev, 0x03); /* DW_AT_name */
    asm_buf_uleb(&abbrev, 0x08); /* DW_FORM_string */
    asm_buf_uleb(&abbrev, 0x11); /* DW_AT_low_pc */
    asm_buf_uleb(&abbrev, 0x01); /* DW_FORM_addr */
    asm_buf_uleb(&abbrev, 0x12); /* DW_AT_high_pc */
    asm_buf_uleb(&abbrev, 0x01); /* DW_FORM_addr */
    asm_buf_byte(&abbrev, 0);
    asm_buf_byte(&abbrev, 0);
    asm_buf_byte(&abbrev, 0); /* end of abbreviation table */

    /* .debug_info with one compile unit and one DIE per global label. */
    ulen_pos = info.len;
    asm_buf_u32(&info, 0); /* unit_length, patched below */
    asm_buf_u16(&info, 4); /* version */
    asm_buf_u32(&info, 0); /* debug_abbrev_offset */
    asm_buf_byte(&info, 8); /* address_size */
    asm_buf_uleb(&info, 1); /* abbreviation 1: compile unit */
    {
        const char *cu_name = files.len > 0 ? ((char **)files.items)[0] : "unknown";

        asm_buf_bytes(&info, cu_name, strlen(cu_name) + 1);
    }
    asm_buf_u32(&info, 0); /* DW_AT_stmt_list */
    asm_buf_u64(&info, ASM_BASE_ADDR);
    asm_buf_u64(&info, ASM_BASE_ADDR + (uint64_t)text.len);
    {
        struct asm_func *funcs = NULL;
        size_t func_count = 0;

        for (i = 0; i < globals.len; i++)
        {
            const char *name = ((char **)globals.items)[i];

            if (asm_label_find(&labels, asm_slice_cstr(name)) != NULL)
                func_count++;
        }
        if (func_count > 0)
        {
            funcs = malloc(func_count * sizeof *funcs);
            if (funcs == NULL)
                goto oom;
            func_count = 0;
            for (i = 0; i < globals.len; i++)
            {
                const char *name = ((char **)globals.items)[i];
                const struct asm_label *label = asm_label_find(&labels, asm_slice_cstr(name));

                if (label != NULL)
                {
                    funcs[func_count].name = name;
                    funcs[func_count].offset = label->offset;
                    func_count++;
                }
            }
            qsort(funcs, func_count, sizeof *funcs, asm_compare_funcs);
            for (i = 0; i < func_count; i++)
            {
                uint64_t high = (i + 1 < func_count)
                                    ? ASM_BASE_ADDR + (uint64_t)funcs[i + 1].offset
                                    : ASM_BASE_ADDR + (uint64_t)text.len;

                asm_buf_uleb(&info, 2); /* abbreviation 2: subprogram */
                asm_buf_bytes(&info, funcs[i].name, strlen(funcs[i].name) + 1);
                asm_buf_u64(&info, ASM_BASE_ADDR + (uint64_t)funcs[i].offset);
                asm_buf_u64(&info, high);
            }
            free(funcs);
        }
    }
    asm_buf_byte(&info, 0); /* end of children */
    asm_buf_patch_u32(&info, ulen_pos, (uint32_t)(info.len - 4));

    if (text.failed || dbg.failed || abbrev.failed || info.failed)
        goto oom;

    res.text = asm_buf_detach(&text, &res.text_len);
    res.debug_line = asm_buf_detach(&dbg, &res.debug_line_len);
    res.debug_info = asm_buf_detach(&info, &res.debug_info_len);
    res.debug_abbrev = asm_buf_detach(&abbrev, &res.debug_abbrev_len);
    res.symbols = symbols.items;
    res.symbol_count = symbols.len;
    symbols.items = NULL;
    symbols.len = 0;
    symbols.cap = 0;
    res.relocs = relocs.items;
    res.reloc_count = relocs.len;
    relocs.items = NULL;
    relocs.len = 0;
    relocs.cap = 0;
    res.ok = true;
    goto cleanup;

oom:
    asm_set_error(&res, "Out of memory while assembling AArch64 source");
fail:
    /* Encoding failed: drop whatever was built, keep the diagnostic. */
    asm_result_release(&res);
    res.ok = false;

cleanup:
    free(src);
    free(lines.items);
    for (i = 0; i < files.len; i++)
        free(((char **)files.items)[i]);
    free(files.items);
    for (i = 0; i < labels.len; i++)
        free(((struct asm_label *)labels.items)[i].name);
    free(labels.items);
    for (i = 0; i < globals.len; i++)
        free(((char **)globals.items)[i]);
    free(globals.items);
    free(entries.items);
    for (i = 0; i < symbols.len; i++)
        free(((struct tracey_asm_symbol *)symbols.items)[i].name);
    free(symbols.items);
    for (i = 0; i < relocs.len; i++)
        free(((struct tracey_asm_reloc *)relocs.items)[i].symbol);
    free(relocs.items);
    free(sorted_labels);
    asm_buf_free(&text);
    asm_buf_free(&dbg);
    asm_buf_free(&abbrev);
    asm_buf_free(&info);
    return res;
}

/*
 * asm_align8 - round a file offset up to an eight-byte boundary.
 *
 * The symbol table and relocation sections contain 64-bit fields, so their
 * file offsets must be naturally aligned for consumers that mmap the file.
 */
size_t asm_align8(size_t value)
{
    return (value + 7u) & ~(size_t)7u;
}

/* Emit n zero bytes, used to pad between naturally aligned sections. */
bool asm_write_pad(FILE *f, size_t n)
{
    const unsigned char zeros[8] = {0};

    while (n > 0)
    {
        size_t chunk = n > sizeof zeros ? sizeof zeros : n;

        if (fwrite(zeros, 1, chunk, f) != chunk)
            return false;
        n -= chunk;
    }
    return true;
}

/* Append a NUL-terminated string to a string table and return its index. */
size_t asm_strtab_add(struct asm_buf *b, const char *s)
{
    size_t index = b->len;

    asm_buf_bytes(b, s, strlen(s) + 1);
    return index;
}

/*
 * tracey_write_elf_object - serialize an assembly result as ELF64 REL.
 *
 * The object contains .text, .symtab, .strtab, .rela.text and .shstrtab plus
 * the three DWARF sections when they are non-empty. Relocations that refer to
 * an undefined label become references to an undefined global symbol, which
 * the linker resolves against the other input objects.
 *
 * The writer checks the stream error state once after all sections have been
 * emitted; a failure therefore reports false even if the underlying fwrite()
 * calls silently accepted a short write.
 */
bool tracey_write_elf_object(const char *path, const struct tracey_asm_result *r)
{
    struct asm_buf strtab;
    struct asm_buf shstrtab;
    struct tracey_elf_sym *syms = NULL;
    struct tracey_elf_rela *relas = NULL;
    struct tracey_elf_ehdr ehdr;
    struct tracey_elf_shdr shdr;
    size_t sym_count = 1;
    size_t rel_count = 0;
    size_t idx_text;
    size_t idx_symtab;
    size_t idx_strtab;
    size_t idx_rela;
    size_t idx_shstrtab;
    size_t idx_debug_line = 0;
    size_t idx_debug_info = 0;
    size_t idx_debug_abbrev = 0;
    size_t text_off;
    size_t symtab_off;
    size_t strtab_off;
    size_t rela_off;
    size_t shstr_off;
    size_t end_off;
    size_t debug_line_off = 0;
    size_t debug_info_off = 0;
    size_t debug_abbrev_off = 0;
    size_t shdr_off;
    size_t shnum;
    size_t i;
    FILE *f;
    bool ok = true;

    if (path == NULL || r == NULL)
        return false;

    f = fopen(path, "wb");
    if (f == NULL)
    {
        fprintf(stderr, "Error: cannot open output object '%s': %s\n", path, strerror(errno));
        return false;
    }

    asm_buf_init(&strtab);
    asm_buf_init(&shstrtab);
    asm_buf_byte(&strtab, 0);
    asm_buf_byte(&shstrtab, 0);

    idx_text = asm_strtab_add(&shstrtab, ".text");
    idx_symtab = asm_strtab_add(&shstrtab, ".symtab");
    idx_strtab = asm_strtab_add(&shstrtab, ".strtab");
    idx_rela = asm_strtab_add(&shstrtab, ".rela.text");
    idx_shstrtab = asm_strtab_add(&shstrtab, ".shstrtab");
    if (r->debug_line != NULL && r->debug_line_len > 0)
        idx_debug_line = asm_strtab_add(&shstrtab, ".debug_line");
    if (r->debug_info != NULL && r->debug_info_len > 0)
        idx_debug_info = asm_strtab_add(&shstrtab, ".debug_info");
    if (r->debug_abbrev != NULL && r->debug_abbrev_len > 0)
        idx_debug_abbrev = asm_strtab_add(&shstrtab, ".debug_abbrev");

    if (shstrtab.failed)
    {
        fprintf(stderr, "Error: out of memory while writing object\n");
        ok = false;
        goto cleanup;
    }

    /*
     * One null symbol followed by the module symbols. External relocation
     * targets discovered below append further undefined symbols.
     */
    if (r->symbol_count > SIZE_MAX - r->reloc_count - 1u)
    {
        fprintf(stderr, "Error: too many symbols while writing object\n");
        ok = false;
        goto cleanup;
    }
    syms = calloc(r->symbol_count + r->reloc_count + 1u, sizeof(*syms));
    relas = calloc(r->reloc_count > 0 ? r->reloc_count : 1u, sizeof(*relas));
    if (syms == NULL || relas == NULL)
    {
        fprintf(stderr, "Error: out of memory while writing object\n");
        ok = false;
        goto cleanup;
    }

    for (i = 0; i < r->symbol_count; i++)
    {
        struct tracey_elf_sym *s = &syms[sym_count];

        s->st_name = (uint32_t)asm_strtab_add(&strtab, r->symbols[i].name);
        s->st_info = (unsigned char)(((r->symbols[i].global ? TRACEY_ELF_STB_GLOBAL
                                                             : TRACEY_ELF_STB_LOCAL)
                                      << 4) |
                                     TRACEY_ELF_STT_FUNC);
        s->st_shndx = 1;
        s->st_value = (uint64_t)r->symbols[i].offset;
        s->st_size = 0;
        sym_count++;
    }

    for (i = 0; i < r->reloc_count; i++)
    {
        size_t si = 0;
        size_t j;

        for (j = 0; j < r->symbol_count; j++)
        {
            if (strcmp(r->symbols[j].name, r->relocs[i].symbol) == 0)
            {
                si = j + 1;
                break;
            }
        }
        if (si == 0)
        {
            struct tracey_elf_sym *s = &syms[sym_count];

            s->st_name = (uint32_t)asm_strtab_add(&strtab, r->relocs[i].symbol);
            s->st_info =
                (unsigned char)((TRACEY_ELF_STB_GLOBAL << 4) | TRACEY_ELF_STT_FUNC);
            s->st_shndx = TRACEY_ELF_SHN_UNDEF;
            s->st_value = 0;
            s->st_size = 0;
            si = sym_count;
            sym_count++;
        }
        relas[rel_count].r_offset = (uint64_t)r->relocs[i].offset;
        relas[rel_count].r_info =
            ((uint64_t)si << 32) | (uint64_t)(uint32_t)r->relocs[i].type;
        relas[rel_count].r_addend = 0;
        rel_count++;
    }

    if (strtab.failed)
    {
        fprintf(stderr, "Error: out of memory while writing object\n");
        ok = false;
        goto cleanup;
    }

    text_off = 64;
    symtab_off = asm_align8(text_off + r->text_len);
    strtab_off = symtab_off + sym_count * sizeof(struct tracey_elf_sym);
    rela_off = asm_align8(strtab_off + strtab.len);
    shstr_off = rela_off + rel_count * sizeof(struct tracey_elf_rela);
    end_off = shstr_off + shstrtab.len;
    if (idx_debug_line != 0)
    {
        debug_line_off = end_off;
        end_off += r->debug_line_len;
    }
    if (idx_debug_info != 0)
    {
        debug_info_off = end_off;
        end_off += r->debug_info_len;
    }
    if (idx_debug_abbrev != 0)
    {
        debug_abbrev_off = end_off;
        end_off += r->debug_abbrev_len;
    }
    shdr_off = asm_align8(end_off);
    shnum = 6u + (idx_debug_line != 0 ? 1u : 0u) + (idx_debug_info != 0 ? 1u : 0u) +
            (idx_debug_abbrev != 0 ? 1u : 0u);

    memset(&ehdr, 0, sizeof ehdr);
    ehdr.e_ident[0] = 0x7f;
    ehdr.e_ident[1] = 'E';
    ehdr.e_ident[2] = 'L';
    ehdr.e_ident[3] = 'F';
    ehdr.e_ident[4] = 2; /* ELFCLASS64 */
    ehdr.e_ident[5] = 1; /* ELFDATA2LSB */
    ehdr.e_ident[6] = 1; /* EV_CURRENT */
    ehdr.e_type = TRACEY_ELF_ET_REL;
    ehdr.e_machine = TRACEY_ELF_EM_AARCH64;
    ehdr.e_version = 1;
    ehdr.e_shoff = (uint64_t)shdr_off;
    ehdr.e_ehsize = 64;
    ehdr.e_shentsize = 64;
    ehdr.e_shnum = (uint16_t)shnum;
    ehdr.e_shstrndx = 5;

    tracey_elf_write_ehdr(f, &ehdr);

    if (fwrite(r->text, 1, r->text_len, f) != r->text_len)
        ok = false;
    if (ok && !asm_write_pad(f, symtab_off - (text_off + r->text_len)))
        ok = false;
    for (i = 0; ok && i < sym_count; i++)
        tracey_elf_write_sym(f, &syms[i]);
    if (ok && fwrite(strtab.data, 1, strtab.len, f) != strtab.len)
        ok = false;
    if (ok && !asm_write_pad(f, rela_off - (strtab_off + strtab.len)))
        ok = false;
    for (i = 0; ok && i < rel_count; i++)
        tracey_elf_write_rela(f, &relas[i]);
    if (ok && fwrite(shstrtab.data, 1, shstrtab.len, f) != shstrtab.len)
        ok = false;
    if (ok && idx_debug_line != 0)
    {
        if (fwrite(r->debug_line, 1, r->debug_line_len, f) != r->debug_line_len)
            ok = false;
    }
    if (ok && idx_debug_info != 0)
    {
        if (fwrite(r->debug_info, 1, r->debug_info_len, f) != r->debug_info_len)
            ok = false;
    }
    if (ok && idx_debug_abbrev != 0)
    {
        if (fwrite(r->debug_abbrev, 1, r->debug_abbrev_len, f) != r->debug_abbrev_len)
            ok = false;
    }
    if (ok && !asm_write_pad(f, shdr_off - end_off))
        ok = false;

    /* Section header table: null entry first. */
    memset(&shdr, 0, sizeof shdr);
    tracey_elf_write_shdr(f, &shdr);

    memset(&shdr, 0, sizeof shdr);
    shdr.sh_name = (uint32_t)idx_text;
    shdr.sh_type = TRACEY_ELF_SHT_PROGBITS;
    shdr.sh_flags = TRACEY_ELF_SHF_ALLOC | TRACEY_ELF_SHF_EXECINSTR;
    shdr.sh_offset = (uint64_t)text_off;
    shdr.sh_size = (uint64_t)r->text_len;
    shdr.sh_addralign = 4;
    tracey_elf_write_shdr(f, &shdr);

    memset(&shdr, 0, sizeof shdr);
    shdr.sh_name = (uint32_t)idx_symtab;
    shdr.sh_type = TRACEY_ELF_SHT_SYMTAB;
    shdr.sh_offset = (uint64_t)symtab_off;
    shdr.sh_size = (uint64_t)(sym_count * sizeof(struct tracey_elf_sym));
    shdr.sh_link = 3;
    shdr.sh_info = 1;
    shdr.sh_addralign = 8;
    shdr.sh_entsize = sizeof(struct tracey_elf_sym);
    tracey_elf_write_shdr(f, &shdr);

    memset(&shdr, 0, sizeof shdr);
    shdr.sh_name = (uint32_t)idx_strtab;
    shdr.sh_type = TRACEY_ELF_SHT_STRTAB;
    shdr.sh_offset = (uint64_t)strtab_off;
    shdr.sh_size = (uint64_t)strtab.len;
    shdr.sh_addralign = 1;
    tracey_elf_write_shdr(f, &shdr);

    memset(&shdr, 0, sizeof shdr);
    shdr.sh_name = (uint32_t)idx_rela;
    shdr.sh_type = TRACEY_ELF_SHT_RELA;
    shdr.sh_offset = (uint64_t)rela_off;
    shdr.sh_size = (uint64_t)(rel_count * sizeof(struct tracey_elf_rela));
    shdr.sh_link = 2;
    shdr.sh_info = 1;
    shdr.sh_addralign = 8;
    shdr.sh_entsize = sizeof(struct tracey_elf_rela);
    tracey_elf_write_shdr(f, &shdr);

    memset(&shdr, 0, sizeof shdr);
    shdr.sh_name = (uint32_t)idx_shstrtab;
    shdr.sh_type = TRACEY_ELF_SHT_STRTAB;
    shdr.sh_offset = (uint64_t)shstr_off;
    shdr.sh_size = (uint64_t)shstrtab.len;
    shdr.sh_addralign = 1;
    tracey_elf_write_shdr(f, &shdr);

    if (idx_debug_line != 0)
    {
        memset(&shdr, 0, sizeof shdr);
        shdr.sh_name = (uint32_t)idx_debug_line;
        shdr.sh_type = TRACEY_ELF_SHT_PROGBITS;
        shdr.sh_offset = (uint64_t)debug_line_off;
        shdr.sh_size = (uint64_t)r->debug_line_len;
        shdr.sh_addralign = 1;
        tracey_elf_write_shdr(f, &shdr);
    }
    if (idx_debug_info != 0)
    {
        memset(&shdr, 0, sizeof shdr);
        shdr.sh_name = (uint32_t)idx_debug_info;
        shdr.sh_type = TRACEY_ELF_SHT_PROGBITS;
        shdr.sh_offset = (uint64_t)debug_info_off;
        shdr.sh_size = (uint64_t)r->debug_info_len;
        shdr.sh_addralign = 1;
        tracey_elf_write_shdr(f, &shdr);
    }
    if (idx_debug_abbrev != 0)
    {
        memset(&shdr, 0, sizeof shdr);
        shdr.sh_name = (uint32_t)idx_debug_abbrev;
        shdr.sh_type = TRACEY_ELF_SHT_PROGBITS;
        shdr.sh_offset = (uint64_t)debug_abbrev_off;
        shdr.sh_size = (uint64_t)r->debug_abbrev_len;
        shdr.sh_addralign = 1;
        tracey_elf_write_shdr(f, &shdr);
    }

    if (ok && ferror(f))
        ok = false;

cleanup:
    if (fclose(f) != 0)
        ok = false;
    if (!ok)
        fprintf(stderr, "Error: failed to write ELF object '%s': %s\n", path,
                strerror(errno));
    asm_buf_free(&strtab);
    asm_buf_free(&shstrtab);
    free(syms);
    free(relas);
    return ok;
}
