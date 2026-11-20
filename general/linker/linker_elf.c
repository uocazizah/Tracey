#include "linker_internal.h"

bool link_object_read(const char *path, struct link_object *object)
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
