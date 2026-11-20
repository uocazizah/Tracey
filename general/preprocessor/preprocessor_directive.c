#include "preprocessor_internal.h"

/* ------------------------------------------------------------------ */
/* Directive evaluation                                               */
/* ------------------------------------------------------------------ */

bool tracey_is_defined_builtin(const char *name)
{
    /* Builtin macros that Tracey pretends to see from the host compiler. */
    return strcmp(name, "__GNUC__") == 0 ||
           strcmp(name, "__GNUC_MINOR__") == 0 ||
           strcmp(name, "__GNUC_PATCHLEVEL__") == 0 ||
           strcmp(name, "__SIZE_TYPE__") == 0;
}

bool tracey_eval_defined(const char *name, struct tracey_macro_table *table)
{
    return tracey_macro_find(table, name) >= 0 || tracey_is_defined_builtin(name);
}

/* ------------------------------------------------------------------ */
/* Directive handling                                                 */
/* ------------------------------------------------------------------ */

bool tracey_handle_include(const char *line, size_t line_len, const char *base_dir,
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

bool tracey_handle_define(const char *line, size_t line_len,
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
    bool variadic = false;
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

            /* Variadic ellipsis as a standalone parameter. */
            if (i + 2 < line_len && line[i] == '.' && line[i + 1] == '.' &&
                line[i + 2] == '.')
            {
                variadic = true;
                if (param_count == param_cap)
                {
                    param_cap *= 2;
                    params = (char **)realloc(params, param_cap * sizeof(char *));
                    if (params == NULL)
                        break;
                }
                params[param_count] = strdup("__VA_ARGS__");
                param_count++;
                i += 3;
                while (i < line_len && tracey_is_space(line[i]))
                    i++;
                break;
            }

            size_t pstart = i;
            while (i < line_len && tracey_is_id_cont(line[i]))
                i++;
            if (pstart == i)
                break; /* not a valid parameter name */

            /* Named variadic parameter: name followed by ellipsis. */
            {
                size_t j = i;
                while (j < line_len && tracey_is_space(line[j]))
                    j++;
                if (j + 2 < line_len && line[j] == '.' && line[j + 1] == '.' &&
                    line[j + 2] == '.')
                {
                    variadic = true;
                    i = j + 3;
                }
            }

            if (param_count == param_cap)
            {
                param_cap *= 2;
                params = (char **)realloc(params, param_cap * sizeof(char *));
                if (params == NULL)
                    break;
            }
            params[param_count] = tracey_strdup_range(line + pstart, i - pstart - (variadic ? 3 : 0));
            /* Trim trailing dots if present in captured name. */
            if (variadic)
            {
                char *p = params[param_count];
                size_t plen = strlen(p);
                while (plen > 0 && p[plen - 1] == '.')
                    p[--plen] = '\0';
            }
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
                                  function_like, variadic);
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

bool tracey_handle_undef(const char *line, size_t line_len,
                                struct tracey_macro_table *table)
{
    size_t i = 0;

    while (i < line_len && tracey_is_space(line[i]))
        i++;

    if (line_len - i < 6 || memcmp(line + i, "#undef", 6) != 0)
        return false;

    i += 6;
    while (i < line_len && tracey_is_space(line[i]))
        i++;

    size_t name_start = i;
    while (i < line_len && tracey_is_id_cont(line[i]))
        i++;
    if (i > name_start)
    {
        char *name = tracey_strdup_range(line + name_start, i - name_start);
        if (name != NULL)
        {
            tracey_macro_undef(table, name);
            free(name);
        }
    }
    return true;
}

bool tracey_handle_error_directive(const char *line, size_t line_len,
                                          bool warning, bool *had_error)
{
    const char *directive = warning ? "#warning" : "#error";
    size_t dlen = warning ? 8 : 6;
    size_t i = 0;

    while (i < line_len && tracey_is_space(line[i]))
        i++;

    if (line_len - i < dlen || memcmp(line + i, directive, dlen) != 0)
        return false;

    i += dlen;
    while (i < line_len && tracey_is_space(line[i]))
        i++;

    /* Print the (trimmed) message text. */
    size_t msg_start = i;
    size_t msg_end = line_len;
    while (msg_end > msg_start && tracey_is_space(line[msg_end - 1]))
        msg_end--;

    if (warning)
        fprintf(stderr, "warning: ");
    else
        fprintf(stderr, "error: ");
    if (msg_end > msg_start)
        fprintf(stderr, "%.*s", (int)(msg_end - msg_start), line + msg_start);
    fprintf(stderr, "\n");

    if (!warning && had_error != NULL)
        *had_error = true;
    return true;
}

bool tracey_handle_ifdef(const char *line, size_t line_len,
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

char *tracey_replace_defined(const char *text, struct tracey_macro_table *table)
{
    size_t len = strlen(text);
    size_t out_cap = len + 1;
    size_t out_len = 0;
    char *out = (char *)malloc(out_cap);
    size_t i = 0;

    if (out == NULL)
        return strdup(text);
    out[0] = '\0';

    while (i < len)
    {
        if (tracey_is_id_start(text[i]))
        {
            size_t start = i;
            while (i < len && tracey_is_id_cont(text[i]))
                i++;
            size_t nlen = i - start;
            char *name = tracey_strdup_range(text + start, nlen);
            if (strcmp(name, "defined") == 0)
            {
                /* Skip spaces. */
                while (i < len && tracey_is_space(text[i]))
                    i++;
                if (i < len && text[i] == '(')
                {
                    i++;
                    while (i < len && tracey_is_space(text[i]))
                        i++;
                    size_t nstart = i;
                    while (i < len && tracey_is_id_cont(text[i]))
                        i++;
                    char *mname = tracey_strdup_range(text + nstart, i - nstart);
                    while (i < len && tracey_is_space(text[i]))
                        i++;
                    if (i < len && text[i] == ')')
                        i++;
                    const char *val = tracey_eval_defined(mname, table) ? "1" : "0";
                    tracey_append_buf(&out, &out_len, &out_cap, val, strlen(val));
                    free(mname);
                }
                else
                {
                    size_t nstart = i;
                    while (i < len && tracey_is_id_cont(text[i]))
                        i++;
                    char *mname = tracey_strdup_range(text + nstart, i - nstart);
                    const char *val = tracey_eval_defined(mname, table) ? "1" : "0";
                    tracey_append_buf(&out, &out_len, &out_cap, val, strlen(val));
                    free(mname);
                }
                free(name);
                continue;
            }
            /* Not defined: copy identifier as-is (macro expansion later). */
            tracey_append_buf(&out, &out_len, &out_cap, name, nlen);
            free(name);
            continue;
        }
        if (out_len + 1 > out_cap)
        {
            out_cap = out_len + 2;
            char *tmp = (char *)realloc(out, out_cap);
            if (tmp != NULL)
                out = tmp;
        }
        out[out_len++] = text[i++];
        out[out_len] = '\0';
    }
    return out;
}

bool tracey_eval_simple_cond(const char *s)
{
    size_t i = 0;
    size_t len = strlen(s);
    bool negate = false;
    bool neg = false;
    long val = 0;

    while (i < len && tracey_is_space(s[i]))
        i++;
    if (i < len && s[i] == '!')
    {
        negate = true;
        i++;
        while (i < len && tracey_is_space(s[i]))
            i++;
    }
    if (i < len && s[i] == '-')
    {
        neg = true;
        i++;
    }
    while (i < len && s[i] >= '0' && s[i] <= '9')
    {
        val = val * 10 + (s[i] - '0');
        i++;
    }
    if (neg)
        val = -val;
    bool res = (val != 0);
    if (negate)
        res = !res;
    return res;
}

bool tracey_handle_if(const char *line, size_t line_len,
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

    /* Expand defined() operators and macros in the condition, then
     * evaluate the resulting (minimal) integer expression. */
    char *cond_raw = tracey_strdup_range(line + i, line_len - i);
    char *cond_defined = tracey_replace_defined(cond_raw ? cond_raw : "", table);
    free(cond_raw);
    int depth = 0;
    char *cond_expanded = tracey_expand_macros(cond_defined ? cond_defined : "", table, &depth);
    free(cond_defined);
    bool condition = tracey_eval_simple_cond(cond_expanded ? cond_expanded : "");
    free(cond_expanded);

    tracey_if_push(stack, condition, parent_active);
    return true;
}

bool tracey_handle_else(const char *line, size_t line_len,
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

bool tracey_handle_elif(const char *line, size_t line_len,
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

            char *cond_raw = tracey_strdup_range(line + i, line_len - i);
            char *cond_defined = tracey_replace_defined(cond_raw ? cond_raw : "", table);
            free(cond_raw);
            int cdepth = 0;
            char *cond_expanded = tracey_expand_macros(cond_defined ? cond_defined : "", table, &cdepth);
            free(cond_defined);
            condition = tracey_eval_simple_cond(cond_expanded ? cond_expanded : "");
            free(cond_expanded);

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

bool tracey_handle_endif(const char *line, size_t line_len,
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

bool tracey_handle_defined_directive(const char *line, size_t line_len,
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
        tracey_macro_define(table, name, replacement, NULL, 0, false, false);
    }

    free(name);
    return true;
}

