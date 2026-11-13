#include "tracey/argument.h"

#include <stdlib.h>
#include <string.h>

static char *tracey_strdup_safe(const char *s)
{
    char *copy;

    if (s == NULL)
        return NULL;
    copy = strdup(s);
    return copy;
}

static enum tracey_args_error tracey_args_push(char ***items, size_t *count, const char *value)
{
    char **new_items;
    char *copy;

    copy = tracey_strdup_safe(value);
    if (copy == NULL)
        return TRACEY_ARGS_NOMEM;

    new_items = (char **)realloc(*items, (*count + 1) * sizeof(char *));
    if (new_items == NULL)
    {
        free(copy);
        return TRACEY_ARGS_NOMEM;
    }

    new_items[*count] = copy;
    *items = new_items;
    (*count)++;
    return TRACEY_ARGS_OK;
}

static enum tracey_args_error parse_option_with_arg(int *i, int argc, char **argv, const char **out)
{
    const char *val;

    if (*i + 1 >= argc)
        return TRACEY_ARGS_MISSING_ARG;
    (*i)++;
    val = argv[*i];
    if (val == NULL || val[0] == '\0')
        return TRACEY_ARGS_EMPTY_ARG;
    *out = val;
    return TRACEY_ARGS_OK;
}

static enum tracey_args_error parse_opt_level_str(const char *level, enum tracey_opt_level *out)
{
    if (level == NULL || strlen(level) != 1 || level[0] < '0' || level[0] > '3')
        return TRACEY_ARGS_INVALID;
    *out = (enum tracey_opt_level)(level[0] - '0');
    return TRACEY_ARGS_OK;
}

enum tracey_args_error tracey_args_parse(int argc, char **argv, const struct tracey_args_config *config,
                                         struct tracey_args *out)
{
    struct tracey_args_config cfg;
    int i;
    enum tracey_args_error err;

    if (out == NULL)
        return TRACEY_ARGS_INVALID;
    tracey_args_init(out);

    if (argc < 0 || argv == NULL)
        return TRACEY_ARGS_INVALID;

    cfg.program_name = "tracey";
    cfg.version_string = "1.0.0";
    cfg.description = NULL;
    cfg.max_args = 10000;
    cfg.allow_unknown_options = false;
    if (config != NULL)
        cfg = *config;

    if ((size_t)argc > cfg.max_args)
        return TRACEY_ARGS_TOO_MANY;

    out->program_name = tracey_strdup_safe(cfg.program_name ? cfg.program_name : "tracey");
    out->version_string = tracey_strdup_safe(cfg.version_string ? cfg.version_string : "1.0.0");
    out->description = tracey_strdup_safe(cfg.description ? cfg.description : "");
    if (out->program_name == NULL || out->version_string == NULL || out->description == NULL)
    {
        tracey_args_free(out);
        return TRACEY_ARGS_NOMEM;
    }

    for (i = 1; i < argc; i++)
    {
        const char *arg = argv[i];
        if (arg == NULL)
            continue;

        if (strcmp(arg, "-h") == 0 || strcmp(arg, "--help") == 0)
        {
            out->help = true;
            return TRACEY_ARGS_OK;
        }
        if (strcmp(arg, "-v") == 0 || strcmp(arg, "--version") == 0)
        {
            out->version = true;
            return TRACEY_ARGS_OK;
        }
        if (strcmp(arg, "-V") == 0 || strcmp(arg, "--verbose") == 0)
        {
            out->verbose = true;
        }
        else if (strcmp(arg, "-Werror") == 0)
        {
            out->werror = true;
        }
        else if (strcmp(arg, "-g") == 0)
        {
            out->debug_info = true;
        }
        else if (strcmp(arg, "-S") == 0 || strcmp(arg, "--assembly") == 0)
        {
            out->assembly = true;
        }
        else if (strcmp(arg, "-o") == 0 || strcmp(arg, "--output") == 0)
        {
            const char *val = NULL;
            err = parse_option_with_arg(&i, argc, argv, &val);
            if (err != TRACEY_ARGS_OK)
                goto fail;
            free(out->output);
            out->output = tracey_strdup_safe(val);
            if (out->output == NULL)
            {
                err = TRACEY_ARGS_NOMEM;
                goto fail;
            }
        }
        else if (strcmp(arg, "-I") == 0 || strcmp(arg, "--include") == 0)
        {
            const char *val = NULL;
            err = parse_option_with_arg(&i, argc, argv, &val);
            if (err != TRACEY_ARGS_OK)
                goto fail;
            err = tracey_args_push(&out->include_paths, &out->include_path_count, val);
            if (err != TRACEY_ARGS_OK)
                goto fail;
        }
        else if (strcmp(arg, "-D") == 0 || strcmp(arg, "--define") == 0)
        {
            const char *val = NULL;
            err = parse_option_with_arg(&i, argc, argv, &val);
            if (err != TRACEY_ARGS_OK)
                goto fail;
            err = tracey_args_push(&out->defines, &out->define_count, val);
            if (err != TRACEY_ARGS_OK)
                goto fail;
        }
        else if (strncmp(arg, "-O", 2) == 0)
        {
            if (arg[2] != '\0')
            {
                err = parse_opt_level_str(arg + 2, &out->optimization);
            }
            else
            {
                const char *val = NULL;
                err = parse_option_with_arg(&i, argc, argv, &val);
                if (err == TRACEY_ARGS_OK)
                    err = parse_opt_level_str(val, &out->optimization);
            }
            if (err != TRACEY_ARGS_OK)
                goto fail;
        }
        else if (strcmp(arg, "--optimize") == 0)
        {
            const char *val = NULL;
            err = parse_option_with_arg(&i, argc, argv, &val);
            if (err != TRACEY_ARGS_OK)
                goto fail;
            err = parse_opt_level_str(val, &out->optimization);
            if (err != TRACEY_ARGS_OK)
                goto fail;
        }
        else if (strncmp(arg, "--optimize=", 11) == 0)
        {
            err = parse_opt_level_str(arg + 11, &out->optimization);
            if (err != TRACEY_ARGS_OK)
                goto fail;
        }
        else if (arg[0] == '-')
        {
            if (!cfg.allow_unknown_options)
            {
                err = TRACEY_ARGS_INVALID;
                goto fail;
            }
        }
        else
        {
            if (arg[0] == '\0')
            {
                err = TRACEY_ARGS_EMPTY_ARG;
                goto fail;
            }
            err = tracey_args_push(&out->inputs, &out->input_count, arg);
            if (err != TRACEY_ARGS_OK)
                goto fail;
        }
    }

    return TRACEY_ARGS_OK;

fail:
    tracey_args_free(out);
    return err;
}
