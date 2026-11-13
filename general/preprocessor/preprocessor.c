#include "tracey/preprocessor.h"

#include "tracey/common.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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

static bool tracey_preprocess_recursive(const char *content, size_t content_len,
                                        const char *base_dir,
                                        const char *const *include_paths, size_t include_path_count,
                                        struct tracey_visited_set *visited,
                                        struct tracey_string *out, bool *had_error);

static bool tracey_handle_include(const char *line, size_t line_len, const char *base_dir,
                                  const char *const *include_paths, size_t include_path_count,
                                  struct tracey_visited_set *visited,
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
                                        include_path_count, visited, out, had_error);
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

static bool tracey_preprocess_recursive(const char *content, size_t content_len,
                                        const char *base_dir,
                                        const char *const *include_paths, size_t include_path_count,
                                        struct tracey_visited_set *visited,
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

        if (!tracey_handle_include(content + line_start, line_end - line_start, base_dir,
                                   include_paths, include_path_count, visited, out, had_error))
        {
            tracey_string_append(out, content + line_start, line_end - line_start);
            tracey_string_append_char(out, '\n');
        }
    }

    return true;
}

struct tracey_preprocess_result tracey_preprocess(const char *source, size_t source_len,
                                                  const struct tracey_preprocessor_config *config)
{
    struct tracey_preprocess_result result;
    struct tracey_visited_set visited;
    struct tracey_string out;
    const char *base_dir = ".";
    const char *const *include_paths = NULL;
    size_t include_path_count = 0;

    result.content = NULL;
    result.had_error = false;

    tracey_visited_init(&visited);
    tracey_string_init(&out);

    if (config != NULL)
    {
        if (config->base_dir != NULL)
            base_dir = config->base_dir;
        include_paths = config->include_paths;
        include_path_count = config->include_path_count;
    }

    tracey_preprocess_recursive(source ? source : "", source_len, base_dir, include_paths,
                                include_path_count, &visited, &out, &result.had_error);

    result.content = out.data;
    out.data = NULL;
    out.len = 0;
    out.cap = 0;

    tracey_string_free(&out);
    tracey_visited_free(&visited);
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
