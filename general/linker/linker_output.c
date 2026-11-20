#include "linker_internal.h"

/*
 * link_write_zeros - write count zero bytes to file.
 */
bool link_write_zeros(FILE *file, uint64_t count)
{
    unsigned char zeros[4096];
    uint64_t remaining = count;

    memset(zeros, 0, sizeof(zeros));
    while (remaining > 0)
    {
        size_t chunk = (remaining > sizeof(zeros)) ? sizeof(zeros) : (size_t)remaining;
        if (fwrite(zeros, 1, chunk, file) != chunk)
            return false;
        remaining -= chunk;
    }
    return true;
}

/*
 * link_write_padding - pad file from cursor up to target offset with zeros.
 */
bool link_write_padding(FILE *file, uint64_t cursor, uint64_t target)
{
    if (target < cursor)
        return false;
    return link_write_zeros(file, target - cursor);
}

/*
 * link_write_bytes - write a byte buffer to file.
 */
bool link_write_bytes(FILE *file, const void *data, size_t size)
{
    if (size == 0)
        return true;
    return fwrite(data, 1, size, file) == size;
}

/*
 * link_write_output - emit the final ELF64 executable.
 *
 * Builds the output symbol table, string tables and section headers, then
 * writes the complete image to disk. The debug sections are borrowed from the
 * first input object that carries them.
 */
bool link_write_output(const char *output, struct link_buffer *text,
                      struct link_symbol_table *table,
                      const unsigned char *debug_line, size_t debug_line_size,
                      const unsigned char *debug_info, size_t debug_info_size,
                      const unsigned char *debug_abbrev, size_t debug_abbrev_size)
{
    struct link_output_symbol *output_symbols = NULL;
    size_t output_symbol_count = 0;
    size_t output_symbol_capacity = 0;
    struct tracey_elf_sym *symbol_table = NULL;
    struct link_buffer strtab;
    struct link_buffer shstrtab;
    bool needs_startup;
    uint64_t entry;
    size_t i;
    bool ok = false;

    buffer_init(&strtab);
    buffer_init(&shstrtab);

    needs_startup = (link_symbol_table_find(table, "_start") == SIZE_MAX);

    /*
     * Select the symbols that go into the output .symtab: defined global
     * symbols with a real name, excluding compiler-generated ".L" labels, as
     * the previous implementation did. The list is sorted by name so the table
     * is deterministic and follows the same order std::map produced.
     */
    for (i = 0; i < table->count; i++)
    {
        const struct link_resolved_symbol *symbol = &table->items[i];
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

    /* Entry point: the stub, or the program's own _start/main. */
    entry = LINK_CODE_BASE_VA;
    if (!needs_startup)
    {
        size_t start_index = link_symbol_table_find(table, "_start");
        size_t main_index = link_symbol_table_find(table, "main");

        if (start_index != SIZE_MAX)
            entry = table->items[start_index].address;
        else if (main_index != SIZE_MAX)
            entry = table->items[main_index].address;
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
            uint64_t offset = LINK_TEXT_FILE_OFFSET + (uint64_t)text->size;
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
                phdr.p_filesz = LINK_TEXT_FILE_OFFSET + (uint64_t)text->size;
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
                if (text->size > 0 &&
                    fwrite(text->data, 1, text->size, file) != text->size)
                {
                    link_diagnostic("cannot write '%s'\n", output);
                    fclose(file);
                    goto done;
                }
                cursor = LINK_TEXT_FILE_OFFSET + (uint64_t)text->size;

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
                    shdr.sh_size = (uint64_t)text->size;
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
    buffer_free(&shstrtab);
    buffer_free(&strtab);
    return ok;
}
