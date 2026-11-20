#include "linker_internal.h"

/*
 * link_symbol_table_init - place the link-wide symbol table in the empty state.
 */
void link_symbol_table_init(struct link_symbol_table *table)
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
void link_symbol_table_free(struct link_symbol_table *table)
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
size_t link_symbol_table_find(const struct link_symbol_table *table, const char *name)
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
bool link_symbol_table_add(struct link_symbol_table *table, const char *name,
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
int link_output_symbol_compare(const void *lhs, const void *rhs)
{
    const struct link_output_symbol *left = (const struct link_output_symbol *)lhs;
    const struct link_output_symbol *right = (const struct link_output_symbol *)rhs;

    return strcmp(left->name, right->name);
}
