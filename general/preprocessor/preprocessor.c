#include "preprocessor_internal.h"

/* ------------------------------------------------------------------ */
/* Recursive preprocessing                                            */
/* ------------------------------------------------------------------ */

bool tracey_preprocess_recursive(const char *content, size_t content_len,
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
        if (active && tracey_handle_undef(line + i, line_len - i, table))
            return true;
        if (active && tracey_handle_error_directive(line + i, line_len - i, false,
                                                    had_error))
            return true;
        if (active && tracey_handle_error_directive(line + i, line_len - i, true,
                                                    had_error))
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

bool tracey_preprocess_recursive(const char *content, size_t content_len,
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
