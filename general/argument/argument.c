#include "tracey/argument.h"

#include <stdlib.h>
#include <string.h>

void tracey_args_init(struct tracey_args *args)
{
    if (args == NULL)
        return;
    memset(args, 0, sizeof(*args));
    args->optimization = TRACEY_OPT_NONE;
}

void tracey_args_free(struct tracey_args *args)
{
    size_t i;

    if (args == NULL)
        return;

    for (i = 0; i < args->input_count; i++)
        free(args->inputs[i]);
    free(args->inputs);

    for (i = 0; i < args->include_path_count; i++)
        free(args->include_paths[i]);
    free(args->include_paths);

    for (i = 0; i < args->define_count; i++)
        free(args->defines[i]);
    free(args->defines);

    free(args->output);
    free(args->program_name);
    free(args->version_string);
    free(args->description);

    tracey_args_init(args);
}

const char *tracey_args_find_define(const struct tracey_args *args, const char *name)
{
    size_t i;
    size_t name_len;

    if (args == NULL || name == NULL || name[0] == '\0')
        return NULL;

    name_len = strlen(name);
    for (i = 0; i < args->define_count; i++)
    {
        const char *def = args->defines[i];
        if (def == NULL)
            continue;
        if (strcmp(def, name) == 0)
            return def;
        if (strncmp(def, name, name_len) == 0 && def[name_len] == '=')
            return def;
    }

    return NULL;
}

bool tracey_args_has_include_path(const struct tracey_args *args, const char *path)
{
    size_t i;

    if (args == NULL || path == NULL)
        return false;

    for (i = 0; i < args->include_path_count; i++)
    {
        if (args->include_paths[i] != NULL && strcmp(args->include_paths[i], path) == 0)
            return true;
    }

    return false;
}

const char *tracey_args_error_string(enum tracey_args_error err)
{
    switch (err)
    {
        case TRACEY_ARGS_OK:
            return "No error";
        case TRACEY_ARGS_NOMEM:
            return "Out of memory while parsing command-line arguments";
        case TRACEY_ARGS_INVALID:
            return "Invalid command-line argument";
        case TRACEY_ARGS_TOO_MANY:
            return "Too many command-line arguments";
        case TRACEY_ARGS_MISSING_ARG:
            return "Missing required command-line argument";
        case TRACEY_ARGS_EMPTY_ARG:
            return "Empty command-line argument is not allowed";
        default:
            return "Unknown command-line argument error";
    }
}
