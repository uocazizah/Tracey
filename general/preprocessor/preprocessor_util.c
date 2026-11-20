#include "preprocessor_internal.h"

/* ------------------------------------------------------------------ */
/* Macro table                                                        */
/* ------------------------------------------------------------------ */



void tracey_macro_table_init(struct tracey_macro_table *table)
{
    table->items = NULL;
    table->count = 0;
    table->cap = 0;
}

void tracey_macro_table_free(struct tracey_macro_table *table)
{
    size_t i;
    for (i = 0; i < table->count; i++)
    {
        free(table->items[i].name);
        free(table->items[i].replacement);
        for (size_t j = 0; j < table->items[i].param_count; j++)
            free(table->items[i].params[j]);
        free(table->items[i].params);
    }
    free(table->items);
    table->items = NULL;
    table->count = 0;
    table->cap = 0;
}

int tracey_macro_find(const struct tracey_macro_table *table, const char *name)
{
    size_t i;
    for (i = 0; i < table->count; i++)
    {
        if (strcmp(table->items[i].name, name) == 0)
            return (int)i;
    }
    return -1;
}

void tracey_macro_undef(struct tracey_macro_table *table, const char *name)
{
    int idx = tracey_macro_find(table, name);
    if (idx < 0)
        return;
    free(table->items[idx].name);
    free(table->items[idx].replacement);
    for (size_t j = 0; j < table->items[idx].param_count; j++)
        free(table->items[idx].params[j]);
    free(table->items[idx].params);
    for (size_t j = (size_t)idx; j + 1 < table->count; j++)
        table->items[j] = table->items[j + 1];
    table->count--;
}

bool tracey_macro_define(struct tracey_macro_table *table, const char *name,
                                char *replacement, char **params, size_t param_count,
                                bool function_like, bool variadic)
{
    int idx = tracey_macro_find(table, name);
    if (idx >= 0)
    {
        /* Redefinition: replace existing. */
        free(table->items[idx].name);
        free(table->items[idx].replacement);
        for (size_t j = 0; j < table->items[idx].param_count; j++)
            free(table->items[idx].params[j]);
        free(table->items[idx].params);
        table->items[idx].name = strdup(name);
        table->items[idx].replacement = replacement;
        table->items[idx].params = params;
        table->items[idx].param_count = param_count;
        table->items[idx].function_like = function_like;
        table->items[idx].variadic = variadic;
        table->items[idx].expanding = false;
        return true;
    }

    if (table->count == table->cap)
    {
        size_t new_cap = table->cap ? table->cap * 2 : 8;
        struct tracey_macro *new_items =
            (struct tracey_macro *)realloc(table->items, new_cap * sizeof(*new_items));
        if (new_items == NULL)
            return false;
        table->items = new_items;
        table->cap = new_cap;
    }
    table->items[table->count].name = strdup(name);
    table->items[table->count].replacement = replacement;
    table->items[table->count].params = params;
    table->items[table->count].param_count = param_count;
    table->items[table->count].function_like = function_like;
    table->items[table->count].variadic = variadic;
    table->items[table->count].expanding = false;
    table->count++;
    return true;
}

/* ------------------------------------------------------------------ */
/* Conditional inclusion stack                                        */
/* ------------------------------------------------------------------ */



void tracey_if_stack_init(struct tracey_if_stack *stack)
{
    stack->items = NULL;
    stack->count = 0;
    stack->cap = 0;
}

void tracey_if_stack_free(struct tracey_if_stack *stack)
{
    free(stack->items);
    stack->items = NULL;
    stack->count = 0;
    stack->cap = 0;
}

bool tracey_if_stack_active(const struct tracey_if_stack *stack)
{
    if (stack->count == 0)
        return true;
    return stack->items[stack->count - 1].active;
}

void tracey_if_push(struct tracey_if_stack *stack, bool condition,
                           bool parent_active)
{
    if (stack->count == stack->cap)
    {
        size_t new_cap = stack->cap ? stack->cap * 2 : 8;
        struct tracey_if_entry *new_items =
            (struct tracey_if_entry *)realloc(stack->items,
                                              new_cap * sizeof(*new_items));
        if (new_items == NULL)
            return;
        stack->items = new_items;
        stack->cap = new_cap;
    }
    stack->items[stack->count].active = parent_active && condition;
    stack->items[stack->count].parent_on = parent_active;
    stack->items[stack->count].saw_true = condition;
    stack->count++;
}

void tracey_if_pop(struct tracey_if_stack *stack)
{
    if (stack->count > 0)
        stack->count--;
}

/* ------------------------------------------------------------------ */
/* Visited set (for #include guards)                                  */
/* ------------------------------------------------------------------ */


void tracey_visited_init(struct tracey_visited_set *set)
{
    set->items = NULL;
    set->count = 0;
    set->cap = 0;
}

void tracey_visited_free(struct tracey_visited_set *set)
{
    size_t i;
    for (i = 0; i < set->count; i++)
        free(set->items[i]);
    free(set->items);
    set->items = NULL;
    set->count = 0;
    set->cap = 0;
}

bool tracey_visited_contains(const struct tracey_visited_set *set, const char *path)
{
    size_t i;
    for (i = 0; i < set->count; i++)
    {
        if (strcmp(set->items[i], path) == 0)
            return true;
    }
    return false;
}

void tracey_visited_add(struct tracey_visited_set *set, const char *path)
{
    char *copy;

    if (tracey_visited_contains(set, path))
        return;

    if (set->count == set->cap)
    {
        size_t new_cap = set->cap ? set->cap * 2 : 8;
        char **new_items = (char **)realloc(set->items, new_cap * sizeof(char *));
        if (new_items == NULL)
            abort();
        set->items = new_items;
        set->cap = new_cap;
    }

    copy = strdup(path);
    if (copy == NULL)
        abort();
    set->items[set->count++] = copy;
}

/* ------------------------------------------------------------------ */
/* String helpers                                                     */
/* ------------------------------------------------------------------ */

char *tracey_strdup_range(const char *start, size_t len)
{
    char *s = (char *)malloc(len + 1);
    if (s == NULL)
        return NULL;
    memcpy(s, start, len);
    s[len] = '\0';
    return s;
}

char *tracey_join_path(const char *dir, const char *name)
{
    size_t dir_len;
    size_t name_len;
    char *out;
    bool need_slash;

    if (dir == NULL || dir[0] == '\0')
        return strdup(name);

    dir_len = strlen(dir);
    name_len = strlen(name);
    need_slash = dir[dir_len - 1] != '/';

    out = (char *)malloc(dir_len + (need_slash ? 1 : 0) + name_len + 1);
    if (out == NULL)
        return NULL;

    memcpy(out, dir, dir_len);
    if (need_slash)
        out[dir_len++] = '/';
    memcpy(out + dir_len, name, name_len);
    out[dir_len + name_len] = '\0';
    return out;
}

bool tracey_file_exists(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (f == NULL)
        return false;
    fclose(f);
    return true;
}

char *tracey_read_file_all(const char *path)
{
    FILE *f;
    long size;
    char *buf;
    size_t nread;

    f = fopen(path, "rb");
    if (f == NULL)
        return NULL;
    if (fseek(f, 0, SEEK_END) != 0)
    {
        fclose(f);
        return NULL;
    }
    size = ftell(f);
    if (size < 0)
    {
        fclose(f);
        return NULL;
    }
    if (fseek(f, 0, SEEK_SET) != 0)
    {
        fclose(f);
        return NULL;
    }

    buf = (char *)malloc((size_t)size + 1);
    if (buf == NULL)
    {
        fclose(f);
        return NULL;
    }

    nread = fread(buf, 1, (size_t)size, f);
    fclose(f);
    if (nread != (size_t)size)
    {
        free(buf);
        return NULL;
    }
    buf[size] = '\0';
    return buf;
}

char *tracey_dirname_dup(const char *path)
{
    const char *slash = strrchr(path, '/');
    if (slash == NULL)
        return strdup(".");
    return tracey_strdup_range(path, (size_t)(slash - path));
}

bool tracey_is_id_start(char c)
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
}

bool tracey_is_id_cont(char c)
{
    return tracey_is_id_start(c) || (c >= '0' && c <= '9');
}

bool tracey_is_space(char c)
{
    return c == ' ' || c == '\t' || c == '\r' || c == '\v' || c == '\f';
}

