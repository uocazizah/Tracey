#include "linker_internal.h"

/*
 * link_patch_relocations - apply every recorded relocation in place.
 *
 * The relocation offsets are relative to the object's own .text, so the
 * object base is added first. Every target must be a symbol known globally;
 * a missing definition is a hard error because emitting a zero displacement
 * would produce a silently corrupt image.
 */
bool link_patch_relocations(struct link_object *loaded, size_t object_count,
                            const char *const *objects, const uint64_t *bases,
                            struct link_symbol_table *table, struct link_buffer *text)
{
    size_t i;

    for (i = 0; i < object_count; i++)
    {
        size_t r;

        for (r = 0; r < loaded[i].reloc_count; r++)
        {
            const struct link_reloc *reloc = &loaded[i].relocs[r];
            size_t target_index = link_symbol_table_find(table, reloc->symbol);
            uint64_t pos = bases[i] + reloc->offset;
            uint64_t pc;
            int64_t delta;
            uint32_t instruction;

            if (target_index == SIZE_MAX)
            {
                link_diagnostic("%s: unresolved symbol '%s'\n", objects[i], reloc->symbol);
                return false;
            }
            if (pos > (uint64_t)text->size || (uint64_t)text->size - pos < 4)
            {
                link_diagnostic("%s: relocation at offset %" PRIu64 " is outside .text\n",
                                objects[i], reloc->offset);
                return false;
            }
            instruction = load_u32(text->data + pos);
            pc = LINK_CODE_BASE_VA + pos;
            delta = (int64_t)table->items[target_index].address - (int64_t)pc;

            switch (reloc->type)
            {
            case LINK_R_AARCH64_JUMP26:
            case LINK_R_AARCH64_CALL26:
                if ((delta & 3) != 0)
                {
                    link_diagnostic("%s: relocation against '%s' is not four-byte aligned\n",
                                    objects[i], reloc->symbol);
                    return false;
                }
                if (delta < LINK_JUMP26_MIN_DELTA || delta > LINK_JUMP26_MAX_DELTA)
                {
                    link_diagnostic("%s: relocation against '%s' is out of range for a "
                                    "26-bit branch (%" PRId64 " bytes)\n",
                                    objects[i], reloc->symbol, delta);
                    return false;
                }
                instruction = (instruction & ~LINK_JUMP26_MASK) |
                              (((uint32_t)(delta >> 2)) & LINK_JUMP26_MASK);
                break;
            case LINK_R_AARCH64_CONDBR19:
                if ((delta & 3) != 0)
                {
                    link_diagnostic("%s: relocation against '%s' is not four-byte aligned\n",
                                    objects[i], reloc->symbol);
                    return false;
                }
                if (delta < LINK_CONDBR19_MIN_DELTA || delta > LINK_CONDBR19_MAX_DELTA)
                {
                    link_diagnostic("%s: relocation against '%s' is out of range for a "
                                    "19-bit conditional branch (%" PRId64 " bytes)\n",
                                    objects[i], reloc->symbol, delta);
                    return false;
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
            store_u32(text->data + pos, instruction);
        }
    }
    return true;
}

/*
 * link_emit_startup_stub - prepend a tiny _start stub when none exists.
 *
 * The stub is placed in front of all code, so every symbol address moves up
 * by its size; the relocations above were already resolved against the
 * unpadded layout, which stays consistent because both sides shift by the
 * same amount.
 */
bool link_emit_startup_stub(struct link_symbol_table *table, struct link_buffer *text)
{
    unsigned char stub[(size_t)LINK_STARTUP_STUB_SIZE];
    uint64_t main_address = LINK_CODE_BASE_VA + LINK_STARTUP_STUB_SIZE;
    size_t main_index;
    int64_t delta;
    size_t i;

    for (i = 0; i < table->count; i++)
        table->items[i].address += LINK_STARTUP_STUB_SIZE;

    main_index = link_symbol_table_find(table, "main");
    if (main_index != SIZE_MAX)
        main_address = table->items[main_index].address;

    delta = (int64_t)main_address - (int64_t)LINK_CODE_BASE_VA;
    if ((delta & 3) != 0 || delta < LINK_JUMP26_MIN_DELTA || delta > LINK_JUMP26_MAX_DELTA)
    {
        link_diagnostic("startup stub cannot branch to 'main' (offset %" PRId64
                        " bytes); the program is too large\n",
                        delta);
        return false;
    }

    store_u32(stub, LINK_BL_OPCODE | (((uint32_t)(delta >> 2)) & LINK_JUMP26_MASK));
    store_u32(stub + 4, LINK_STARTUP_MOV_X8);
    store_u32(stub + 8, LINK_STARTUP_SVC0);

    if (!buffer_reserve(text, (size_t)LINK_STARTUP_STUB_SIZE))
    {
        link_diagnostic("out of memory\n");
        return false;
    }
    if (text->size > 0)
        memmove(text->data + LINK_STARTUP_STUB_SIZE, text->data, text->size);
    memcpy(text->data, stub, sizeof(stub));
    text->size += (size_t)LINK_STARTUP_STUB_SIZE;
    return true;
}
