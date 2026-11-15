#include "tracey/preprocessor.h"

#include "tracey/common.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ------------------------------------------------------------------ */
/* Macro table                                                        */
/* ------------------------------------------------------------------ */

struct tracey_macro
{
    char *name;
    char *replacement;
    char **params;
    size_t param_count;
    bool function_like;
};

struct tracey_macro_table
{
    struct tracey_macro *items;
    size_t count;
    size_t cap;
};

static void tracey_macro_table_init(struct tracey_macro_table *table)
{
    table->items = NULL;
    table->count = 0;
    table->cap = 0;
}

static void tracey_macro_table_free(struct tracey_macro_table *table)
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

static int tracey_macro_find(const struct tracey_macro_table *table, const char *name)
{
    size_t i;
    for (i = 0; i < table->count; i++)
    {
        if (strcmp(table->items[i].name, name) == 0)
            return (int)i;
    }
    return -1;
}

static bool tracey_macro_define(struct tracey_macro_table *table, const char *name,
                                char *replacement, char **params, size_t param_count,
                                bool function_like)
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
    table->count++;
    return true;
}

/* ------------------------------------------------------------------ */
/* Conditional inclusion stack                                        */
/* ------------------------------------------------------------------ */

struct tracey_if_entry
{
    bool active;      /* current block is being emitted */
    bool parent_on;   /* parent context was active */
    bool saw_true;    /* some #if in this chain was true */
};

struct tracey_if_stack
{
    struct tracey_if_entry *items;
    size_t count;
    size_t cap;
};

static void tracey_if_stack_init(struct tracey_if_stack *stack)
{
    stack->items = NULL;
    stack->count = 0;
    stack->cap = 0;
}

static void tracey_if_stack_free(struct tracey_if_stack *stack)
{
    free(stack->items);
    stack->items = NULL;
    stack->count = 0;
    stack->cap = 0;
}

static bool tracey_if_stack_active(const struct tracey_if_stack *stack)
{
    if (stack->count == 0)
        return true;
    return stack->items[stack->count - 1].active;
}

static void tracey_if_push(struct tracey_if_stack *stack, bool condition,
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

static void tracey_if_pop(struct tracey_if_stack *stack)
{
    if (stack->count > 0)
        stack->count--;
}

/* ------------------------------------------------------------------ */
/* Visited set (for #include guards)                                  */
/* ------------------------------------------------------------------ */

struct tracey_visited_set
{
    char **items;
    size_t count;
    size_t cap;
};

static void tracey_visited_init(struct tracey_visited_set *set)
{
    set->items = NULL;
    set->count = 0;
    set->cap = 0;
}

static void tracey_visited_free(struct tracey_visited_set *set)
{
    size_t i;
    for (i = 0; i < set->count; i++)
        free(set->items[i]);
    free(set->items);
    set->items = NULL;
    set->count = 0;
    set->cap = 0;
}

static bool tracey_visited_contains(const struct tracey_visited_set *set, const char *path)
{
    size_t i;
    for (i = 0; i < set->count; i++)
    {
        if (strcmp(set->items[i], path) == 0)
            return true;
    }
    return false;
}

static void tracey_visited_add(struct tracey_visited_set *set, const char *path)
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

static char *tracey_strdup_range(const char *start, size_t len)
{
    char *s = (char *)malloc(len + 1);
    if (s == NULL)
        return NULL;
    memcpy(s, start, len);
    s[len] = '\0';
    return s;
}

static char *tracey_join_path(const char *dir, const char *name)
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

static bool tracey_file_exists(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (f == NULL)
        return false;
    fclose(f);
    return true;
}

static char *tracey_read_file_all(const char *path)
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

static char *tracey_dirname_dup(const char *path)
{
    const char *slash = strrchr(path, '/');
    if (slash == NULL)
        return strdup(".");
    return tracey_strdup_range(path, (size_t)(slash - path));
}

static bool tracey_is_id_start(char c)
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
}

static bool tracey_is_id_cont(char c)
{
    return tracey_is_id_start(c) || (c >= '0' && c <= '9');
}

static bool tracey_is_space(char c)
{
    return c == ' ' || c == '\t' || c == '\r' || c == '\v' || c == '\f';
}

/* ------------------------------------------------------------------ */
/* Macro expansion                                                    */
/* ------------------------------------------------------------------ */

static char *tracey_expand_macros(const char *text, struct tracey_macro_table *table,
                                  int *depth)
{
    /* Simple macro expansion: scan for identifiers, replace with macro
     * replacement text. Depth guards against recursive expansion. */
    size_t len = strlen(text);
    size_t out_cap = len + 1;
    size_t out_len = 0;
    char *out = (char *)malloc(out_cap);
    size_t i = 0;

    if (out == NULL)
        return NULL;

    /*
     * Terminate the buffer up front. When text is empty the expansion loop
     * below never executes, so without this the caller's strlen(expanded)
     * would read uninitialized heap memory and copy it into the preprocessed
     * output. Blank source lines take this path, so the terminator is
     * mandatory, not merely defensive.
     */
    out[0] = '\0';

    (*depth)++;
    if (*depth > 64)
    {
        free(out);
        return strdup(text); /* give up on deep recursion */
    }

    while (i < len)
    {
        char c = text[i];

        if (tracey_is_id_start(c))
        {
            size_t start = i;
            while (i < len && tracey_is_id_cont(text[i]))
                i++;
            size_t name_len = i - start;
            char *name = tracey_strdup_range(text + start, name_len);
            int midx = tracey_macro_find(table, name);

            if (midx >= 0 && table->items[midx].function_like)
            {
                /* Function-like macro: parse arguments. */
                size_t j = i;
                while (j < len && tracey_is_space(text[j]))
                    j++;
                if (j < len && text[j] == '(')
                {
                    /* Find matching ')'. */
                    size_t arg_start = j + 1;
                    size_t arg_end = arg_start;
                    int paren_depth = 1;
                    while (arg_end < len && paren_depth > 0)
                    {
                        if (text[arg_end] == '(')
                            paren_depth++;
                        else if (text[arg_end] == ')')
                            paren_depth--;
                        arg_end++;
                    }
                    /* arg_end points past the ')'. */
                    size_t arg_list_len = arg_end - arg_start - 1;
                    char *arg_list = tracey_strdup_range(text + arg_start, arg_list_len);

                    /* Simple single-argument substitution: replace all
                     * occurrences of the param name in the replacement. */
                    char *result = strdup(table->items[midx].replacement);
                    if (table->items[midx].param_count == 1 && arg_list != NULL)
                    {
                        /* Trim surrounding whitespace from argument. */
                        size_t astart = 0;
                        while (astart < strlen(arg_list) && tracey_is_space(arg_list[astart]))
                            astart++;
                        size_t aend = strlen(arg_list);
                        while (aend > astart && tracey_is_space(arg_list[aend - 1]))
                            aend--;
                        arg_list[aend] = '\0';

                        char *replaced = NULL;
                        size_t rcap = strlen(result) + strlen(arg_list) + 1;
                        size_t rlen = 0;
                        replaced = (char *)malloc(rcap);
                        if (replaced != NULL)
                        {
                            size_t k = 0;
                            size_t pname_len = strlen(table->items[midx].params[0]);
                            while (k < strlen(result))
                            {
                                if (strncmp(result + k, table->items[midx].params[0], pname_len) == 0 &&
                                    (k + pname_len == strlen(result) ||
                                     !tracey_is_id_cont(result[k + pname_len])))
                                {
                                    memcpy(replaced + rlen, arg_list + astart, aend - astart);
                                    rlen += aend - astart;
                                    k += pname_len;
                                }
                                else
                                {
                                    replaced[rlen++] = result[k++];
                                }
                            }
                            replaced[rlen] = '\0';
                        }
                        free(result);
                        result = replaced ? replaced : strdup("");
                        free(arg_list);
                    }

                    /* Recursively expand the result. */
                    char *expanded = tracey_expand_macros(result, table, depth);
                    free(result);

                    size_t elen = expanded ? strlen(expanded) : 0;
                    if (out_len + elen + 1 > out_cap)
                    {
                        out_cap = out_len + elen + 1;
                        out = (char *)realloc(out, out_cap);
                    }
                    memcpy(out + out_len, expanded, elen);
                    out_len += elen;
                    out[out_len] = '\0';
                    free(expanded);
                    free(name);
                    i = arg_end;
                    continue;
                }
                /* Not actually a function-like call: fall through to copy as-is. */
            }

            /* Object-like macro: replace with replacement text. */
            if (midx >= 0 && !table->items[midx].function_like)
            {
                char *replacement = strdup(table->items[midx].replacement);
                char *expanded = tracey_expand_macros(replacement, table, depth);
                free(replacement);

                size_t elen = expanded ? strlen(expanded) : 0;
                if (out_len + elen + 1 > out_cap)
                {
                    out_cap = out_len + elen + 1;
                    out = (char *)realloc(out, out_cap);
                }
                memcpy(out + out_len, expanded, elen);
                out_len += elen;
                out[out_len] = '\0';
                free(expanded);
                free(name);
                continue;
            }

            free(name);
            /* Not a macro: copy the identifier as-is. */
            if (out_len + name_len + 1 > out_cap)
            {
                out_cap = out_len + name_len + 1;
                out = (char *)realloc(out, out_cap);
            }
            memcpy(out + out_len, text + start, name_len);
            out_len += name_len;
            out[out_len] = '\0';
            continue;
        }

        /* Regular character: copy as-is. */
        if (out_len + 1 > out_cap)
        {
            out_cap = out_len + 1;
            out = (char *)realloc(out, out_cap);
        }
        out[out_len++] = c;
        out[out_len] = '\0';
        i++;
    }

    (*depth)--;
    return out;
}

/* ------------------------------------------------------------------ */
/* Directive evaluation                                               */
/* ------------------------------------------------------------------ */

static bool tracey_is_defined_builtin(const char *name)
{
    /* Builtin macros that Tracey pretends to see from the host compiler. */
    return strcmp(name, "__GNUC__") == 0 ||
           strcmp(name, "__GNUC_MINOR__") == 0 ||
           strcmp(name, "__GNUC_PATCHLEVEL__") == 0 ||
           strcmp(name, "__SIZE_TYPE__") == 0;
}

static bool tracey_eval_defined(const char *name, struct tracey_macro_table *table)
{
    return tracey_macro_find(table, name) >= 0 || tracey_is_defined_builtin(name);
}

/* ------------------------------------------------------------------ */
/* Directive handling                                                 */
/* ------------------------------------------------------------------ */

static bool tracey_handle_include(const char *line, size_t line_len, const char *base_dir,
                                  const char *const *include_paths, size_t include_path_count,
                                  struct tracey_visited_set *visited,
                                  struct tracey_macro_table *table,
                                  struct tracey_if_stack *if_stack,
                                  struct tracey_string *out, bool *had_error);

static bool tracey_preprocess_recursive(const char *content, size_t content_len,
                                        const char *base_dir,
                                        const char *const *include_paths, size_t include_path_count,
                                        struct tracey_visited_set *visited,
                                        struct tracey_macro_table *table,
                                        struct tracey_if_stack *if_stack,
                                        struct tracey_string *out, bool *had_error);

static bool tracey_handle_define(const char *line, size_t line_len,
                                 struct tracey_macro_table *table);

static bool tracey_handle_include(const char *line, size_t line_len, const char *base_dir,
                                  const char *const *include_paths, size_t include_path_count,
                                  struct tracey_visited_set *visited,
                                  struct tracey_macro_table *table,
                                  struct tracey_if_stack *if_stack,
                                  struct tracey_string *out, bool *had_error)
{
    size_t i = 0;
    size_t name_start = 0;
    size_t name_end = 0;
    bool is_system = false;
    char *incname = NULL;
    char *found = NULL;

    while (i < line_len && (line[i] == ' ' || line[i] == '\t'))
        i++;

    if (line_len - i < 8 || memcmp(line + i, "#include", 8) != 0)
        return false;

    i += 8;
    while (i < line_len && (line[i] == ' ' || line[i] == '\t'))
        i++;

    if (i < line_len && line[i] == '<')
    {
        name_start = i + 1;
        is_system = true;
        for (i = name_start; i < line_len; i++)
        {
            if (line[i] == '>')
            {
                name_end = i;
                break;
            }
        }
    }
    else if (i < line_len && line[i] == '"')
    {
        name_start = i + 1;
        for (i = name_start; i < line_len; i++)
        {
            if (line[i] == '"')
            {
                name_end = i;
                break;
            }
        }
    }

    if (name_end <= name_start)
        return false;

    incname = tracey_strdup_range(line + name_start, name_end - name_start);
    if (incname == NULL)
        abort();

    if (!is_system && base_dir != NULL)
    {
        char *cand = tracey_join_path(base_dir, incname);
        if (cand && tracey_file_exists(cand))
            found = cand;
        else
            free(cand);
    }

    for (i = 0; found == NULL && i < include_path_count; i++)
    {
        char *cand = tracey_join_path(include_paths[i], incname);
        if (cand && tracey_file_exists(cand))
            found = cand;
        else
            free(cand);
    }

    if (found == NULL && is_system)
    {
        const char *sysdirs[] = {"lib", "include"};
        size_t s;
        for (s = 0; s < 2 && found == NULL; s++)
        {
            char *cand = tracey_join_path(sysdirs[s], incname);
            if (cand && tracey_file_exists(cand))
                found = cand;
            else
                free(cand);
        }
    }

    if (found == NULL)
    {
        fprintf(stderr, "Error: cannot find included file '%s'\n", incname);
        fprintf(stderr, "\033[1;34mnote:\033[0m verify the include search paths and file name\n");
        *had_error = true;
        free(incname);
        return true;
    }

    if (tracey_visited_contains(visited, found))
    {
        free(found);
        free(incname);
        return true;
    }

    tracey_visited_add(visited, found);

    {
        char *inc_content = tracey_read_file_all(found);
        char *inc_dir = tracey_dirname_dup(found);
        if (inc_content != NULL)
        {
            size_t inc_len = strlen(inc_content);
            tracey_preprocess_recursive(inc_content, inc_len, inc_dir, include_paths,
                                        include_path_count, visited, table, if_stack, out,
                                        had_error);
            tracey_string_append_char(out, '\n');
        }
        else
        {
            fprintf(stderr, "Error: failed to read included file '%s'\n", found);
            *had_error = true;
        }
        free(inc_dir);
        free(inc_content);
    }

    free(found);
    free(incname);
    return true;
}

static bool tracey_handle_define(const char *line, size_t line_len,
                                 struct tracey_macro_table *table)
{
    size_t i = 0;

    while (i < line_len && tracey_is_space(line[i]))
        i++;

    if (line_len - i < 7 || memcmp(line + i, "#define", 7) != 0)
        return false;

    i += 7;
    while (i < line_len && tracey_is_space(line[i]))
        i++;

    /* Read macro name. */
    if (i >= line_len || !tracey_is_id_start(line[i]))
        return false;
    size_t name_start = i;
    while (i < line_len && tracey_is_id_cont(line[i]))
        i++;
    size_t name_len = i - name_start;
    char *name = tracey_strdup_range(line + name_start, name_len);

    bool function_like = false;
    char **params = NULL;
    size_t param_count = 0;

    /* Skip whitespace. */
    while (i < line_len && tracey_is_space(line[i]))
        i++;

    if (i < line_len && line[i] == '(')
    {
        function_like = true;
        i++;
        params = (char **)malloc(8 * sizeof(char *));
        if (params == NULL)
        {
            free(name);
            return false;
        }
        size_t param_cap = 8;

        while (i < line_len && line[i] != ')')
        {
            while (i < line_len && tracey_is_space(line[i]))
                i++;
            if (i >= line_len || line[i] == ')')
                break;
            size_t pstart = i;
            while (i < line_len && tracey_is_id_cont(line[i]))
                i++;
            if (pstart == i)
                break; /* not a valid parameter name */
            if (param_count == param_cap)
            {
                param_cap *= 2;
                params = (char **)realloc(params, param_cap * sizeof(char *));
                if (params == NULL)
                    break;
            }
            params[param_count] = tracey_strdup_range(line + pstart, i - pstart);
            param_count++;

            while (i < line_len && tracey_is_space(line[i]))
                i++;
            if (i < line_len && line[i] == ',')
                i++;
        }
        if (i < line_len && line[i] == ')')
            i++;
    }

    /* Skip whitespace before replacement. */
    while (i < line_len && tracey_is_space(line[i]))
        i++;

    char *replacement = tracey_strdup_range(line + i, line_len - i);

    bool ok = tracey_macro_define(table, name, replacement, params, param_count,
                                  function_like);
    if (!ok)
    {
        free(name);
        free(replacement);
        for (size_t j = 0; j < param_count; j++)
            free(params[j]);
        free(params);
    }
    free(name);
    return ok;
}

static bool tracey_handle_ifdef(const char *line, size_t line_len,
                                struct tracey_if_stack *stack,
                                struct tracey_macro_table *table, bool parent_active,
                                bool ifndef_mode)
{
    size_t i = 0;
    const char *directive;

    if (ifndef_mode)
        directive = "#ifndef";
    else
        directive = "#ifdef";

    while (i < line_len && tracey_is_space(line[i]))
        i++;

    if (line_len - i < strlen(directive) ||
        memcmp(line + i, directive, strlen(directive)) != 0)
        return false;

    i += strlen(directive);
    while (i < line_len && tracey_is_space(line[i]))
        i++;

    size_t name_start = i;
    while (i < line_len && tracey_is_id_cont(line[i]))
        i++;
    size_t name_len = i - name_start;

    char *name = (name_len > 0) ? tracey_strdup_range(line + name_start, name_len)
                                : strdup("");
    bool defined = tracey_eval_defined(name, table);
    bool condition = ifndef_mode ? !defined : defined;

    tracey_if_push(stack, condition, parent_active);

    free(name);
    return true;
}

static bool tracey_handle_if(const char *line, size_t line_len,
                             struct tracey_if_stack *stack,
                             struct tracey_macro_table *table, bool parent_active)
{
    size_t i = 0;

    while (i < line_len && tracey_is_space(line[i]))
        i++;

    if (line_len - i < 3 || memcmp(line + i, "#if", 3) != 0)
        return false;

    i += 3;
    while (i < line_len && tracey_is_space(line[i]))
        i++;

    /* Very simple #if evaluation: only 'defined(NAME)' and integer
     * comparisons are supported. For now, treat non-zero numeric or
     * defined(NAME) as true, everything else as false. */
    bool condition = false;

    if (strncmp(line + i, "defined", 7) == 0)
    {
        size_t j = i + 7;
        while (j < line_len && tracey_is_space(line[j]))
            j++;
        if (j < line_len && line[j] == '(')
        {
            j++;
            while (j < line_len && tracey_is_space(line[j]))
                j++;
            size_t name_start = j;
            while (j < line_len && tracey_is_id_cont(line[j]))
                j++;
            if (j < line_len && line[j] == ')')
                j++;
            char *name = tracey_strdup_range(line + name_start, j - name_start - 1);
            condition = tracey_eval_defined(name, table);
            free(name);
        }
        else
        {
            /* defined NAME without parens */
            size_t name_start = j;
            while (j < line_len && tracey_is_id_cont(line[j]))
                j++;
            char *name = tracey_strdup_range(line + name_start, j - name_start);
            condition = tracey_eval_defined(name, table);
            free(name);
        }
        i = j;
    }
    else
    {
        /* Numeric expression: parse first integer. */
        long val = 0;
        bool neg = false;
        if (i < line_len && line[i] == '-')
        {
            neg = true;
            i++;
        }
        while (i < line_len && line[i] >= '0' && line[i] <= '9')
        {
            val = val * 10 + (line[i] - '0');
            i++;
        }
        if (neg)
            val = -val;
        condition = (val != 0);
    }

    tracey_if_push(stack, condition, parent_active);
    return true;
}

static bool tracey_handle_else(const char *line, size_t line_len,
                            struct tracey_if_stack *stack)
{
    size_t i = 0;

    while (i < line_len && tracey_is_space(line[i]))
        i++;

    if (line_len - i < 5 || memcmp(line + i, "#else", 5) != 0)
        return false;

    if (stack->count == 0)
        return true;

    {
        struct tracey_if_entry *top = &stack->items[stack->count - 1];
        if (!top->parent_on || top->saw_true)
        {
            top->active = false;
        }
        else
        {
            top->active = true;
            top->saw_true = true;
        }
    }
    return true;
}

static bool tracey_handle_elif(const char *line, size_t line_len,
                              struct tracey_if_stack *stack,
                              struct tracey_macro_table *table)
{
    size_t i = 0;
    bool condition = false;

    while (i < line_len && tracey_is_space(line[i]))
        i++;

    if (line_len - i < 5 || memcmp(line + i, "#elif", 5) != 0)
        return false;

    if (stack->count == 0)
        return true;

    {
        struct tracey_if_entry *top = &stack->items[stack->count - 1];
        if (!top->saw_true)
        {
            i += 5;
            while (i < line_len && tracey_is_space(line[i]))
                i++;

            if (strncmp(line + i, "defined", 7) == 0)
            {
                size_t j = i + 7;
                while (j < line_len && tracey_is_space(line[j]))
                    j++;
                if (j < line_len && line[j] == '(')
                {
                    j++;
                    while (j < line_len && tracey_is_space(line[j]))
                        j++;
                    size_t name_start = j;
                    while (j < line_len && tracey_is_id_cont(line[j]))
                        j++;
                    char *name = tracey_strdup_range(line + name_start, j - name_start);
                    if (j < line_len && line[j] == ')')
                        j++;
                    condition = tracey_eval_defined(name, table);
                    free(name);
                }
                else
                {
                    size_t name_start = j;
                    while (j < line_len && tracey_is_id_cont(line[j]))
                        j++;
                    char *name = tracey_strdup_range(line + name_start, j - name_start);
                    condition = tracey_eval_defined(name, table);
                    free(name);
                }
            }
            else
            {
                long val = 0;
                bool neg = false;
                if (i < line_len && line[i] == '-')
                {
                    neg = true;
                    i++;
                }
                while (i < line_len && line[i] >= '0' && line[i] <= '9')
                {
                    val = val * 10 + (line[i] - '0');
                    i++;
                }
                if (neg)
                    val = -val;
                condition = (val != 0);
            }

            if (top->parent_on && condition)
            {
                top->active = true;
                top->saw_true = true;
            }
            else
            {
                top->active = false;
            }
        }
        else
        {
            top->active = false;
        }
    }
    return true;
}

static bool tracey_handle_endif(const char *line, size_t line_len,
                                struct tracey_if_stack *stack)
{
    size_t i = 0;

    while (i < line_len && tracey_is_space(line[i]))
        i++;

    if (line_len - i < 6 || memcmp(line + i, "#endif", 6) != 0)
        return false;

    tracey_if_pop(stack);
    return true;
}

static bool tracey_handle_defined_directive(const char *line, size_t line_len,
                                            struct tracey_macro_table *table)
{
    size_t i = 0;

    while (i < line_len && tracey_is_space(line[i]))
        i++;

    if (line_len - i < 8 || memcmp(line + i, "#defined", 8) != 0)
        return false;

    i += 8;
    while (i < line_len && tracey_is_space(line[i]))
        i++;

    if (i >= line_len || !tracey_is_id_start(line[i]))
        return false;

    size_t name_start = i;
    while (i < line_len && tracey_is_id_cont(line[i]))
        i++;
    char *name = tracey_strdup_range(line + name_start, i - name_start);

    /* #defined NAME: define NAME only if the host compiler defines it. */
    if (tracey_is_defined_builtin(name))
    {
        char *replacement = strdup("1");
        tracey_macro_define(table, name, replacement, NULL, 0, false);
    }

    free(name);
    return true;
}

/* ------------------------------------------------------------------ */
/* Recursive preprocessing                                            */
/* ------------------------------------------------------------------ */

static bool tracey_preprocess_recursive(const char *content, size_t content_len,
                                        const char *base_dir,
                                        const char *const *include_paths, size_t include_path_count,
                                        struct tracey_visited_set *visited,
                                        struct tracey_macro_table *table,
                                        struct tracey_if_stack *if_stack,
                                        struct tracey_string *out, bool *had_error);

static bool tracey_handle_line(const char *line, size_t line_len, const char *base_dir,
                               const char *const *include_paths, size_t include_path_count,
                               struct tracey_visited_set *visited,
                               struct tracey_macro_table *table,
                               struct tracey_if_stack *if_stack,
                               struct tracey_string *out, bool *had_error)
{
    /* Skip leading whitespace. */
    size_t i = 0;
    while (i < line_len && tracey_is_space(line[i]))
        i++;

    /* Directive line? */
    if (i < line_len && line[i] == '#')
    {
        bool active = tracey_if_stack_active(if_stack);

        if (tracey_handle_include(line + i, line_len - i, base_dir, include_paths,
                                  include_path_count, visited, table, if_stack,
                                  out, had_error))
            return true;
        /* Only process macro definitions when the conditional is active. */
        if (active && tracey_handle_define(line + i, line_len - i, table))
            return true;
        if (tracey_handle_ifdef(line + i, line_len - i, if_stack, table,
                                active, false))
            return true;
        if (tracey_handle_ifdef(line + i, line_len - i, if_stack, table,
                                active, true))
            return true;
        if (tracey_handle_if(line + i, line_len - i, if_stack, table,
                             active))
            return true;
        if (tracey_handle_else(line + i, line_len - i, if_stack))
            return true;
        if (tracey_handle_elif(line + i, line_len - i, if_stack, table))
            return true;
        if (tracey_handle_endif(line + i, line_len - i, if_stack))
            return true;
        if (active && tracey_handle_defined_directive(line + i, line_len - i, table))
            return true;

        /* Unknown directive: ignore. */
        return true;
    }

    /* Non-directive line: only emit if active. */
    if (!tracey_if_stack_active(if_stack))
        return true;

    /* Expand macros and append. */
    char *line_copy = tracey_strdup_range(line, line_len);
    if (line_copy == NULL)
        return true;

    int depth = 0;
    char *expanded = tracey_expand_macros(line_copy, table, &depth);
    free(line_copy);

    if (expanded != NULL)
    {
        tracey_string_append(out, expanded, strlen(expanded));
        tracey_string_append_char(out, '\n');
        free(expanded);
    }
    return true;
}

static bool tracey_preprocess_recursive(const char *content, size_t content_len,
                                        const char *base_dir,
                                        const char *const *include_paths, size_t include_path_count,
                                        struct tracey_visited_set *visited,
                                        struct tracey_macro_table *table,
                                        struct tracey_if_stack *if_stack,
                                        struct tracey_string *out, bool *had_error)
{
    size_t i = 0;

    while (i < content_len)
    {
        size_t line_start = i;
        size_t line_end;

        while (i < content_len && content[i] != '\n')
            i++;
        line_end = i;
        if (i < content_len && content[i] == '\n')
            i++;

        if (!tracey_handle_line(content + line_start, line_end - line_start, base_dir,
                                include_paths, include_path_count, visited, table,
                                if_stack, out, had_error))
        {
            tracey_string_append(out, content + line_start, line_end - line_start);
            tracey_string_append_char(out, '\n');
        }
    }

    return true;
}

/* ------------------------------------------------------------------ */
/* Public API                                                         */
/* ------------------------------------------------------------------ */

struct tracey_preprocess_result tracey_preprocess(const char *source, size_t source_len,
                                                   const struct tracey_preprocessor_config *config)
{
    struct tracey_preprocess_result result;
    struct tracey_visited_set visited;
    struct tracey_string out;
    struct tracey_macro_table table;
    struct tracey_if_stack if_stack;
    const char *base_dir = ".";
    const char *const *include_paths = NULL;
    size_t include_path_count = 0;

    result.content = NULL;
    result.had_error = false;

    tracey_visited_init(&visited);
    tracey_string_init(&out);
    tracey_macro_table_init(&table);
    tracey_if_stack_init(&if_stack);

    if (config != NULL)
    {
        if (config->base_dir != NULL)
            base_dir = config->base_dir;
        include_paths = config->include_paths;
        include_path_count = config->include_path_count;
    }

    tracey_preprocess_recursive(source ? source : "", source_len, base_dir, include_paths,
                                include_path_count, &visited, &table, &if_stack, &out,
                                &result.had_error);

    result.content = out.data;
    out.data = NULL;
    out.len = 0;
    out.cap = 0;

    tracey_string_free(&out);
    tracey_visited_free(&visited);
    tracey_macro_table_free(&table);
    tracey_if_stack_free(&if_stack);
    return result;
}

void tracey_preprocess_result_free(struct tracey_preprocess_result *result)
{
    if (result == NULL)
        return;
    free(result->content);
    result->content = NULL;
    result->had_error = false;
}
