#include <tracey/argument.h>
#include "argument_internal.h"
#include <stdlib.h>
#include <string.h>
#include <limits.h>

#define DEFAULT_MAX_ARGS 10000

/* Forward declarations */
static tracey_args_result_t parse_optimization(const char* arg, int* i, int argc, char** argv, tracey_opt_level_t* out);
static tracey_args_result_t parse_option_with_arg(int* i, int argc, char** argv, const char** out);

tracey_args_result_t tracey_args_parse(tracey_args_t** out_args, int argc, char** argv, const tracey_args_config_t* config)
{
    if (!out_args || argc < 0 || !argv) {
        return TRACEY_ARGS_ERROR_INVALID;
    }

    /* Default configuration */
    tracey_args_config_t default_config;
    default_config.program_name = "tracey";
    default_config.version_string = "1.0.0";
    default_config.description = "Tracey compiler";
    default_config.max_args = DEFAULT_MAX_ARGS;
    default_config.allow_unknown_options = false;
    if (config) default_config = *config;

    if (argc > (int)default_config.max_args) {
        return TRACEY_ARGS_ERROR_TOO_MANY;
    }

    /* Allocate main structure */
    tracey_args_t* args = calloc(1, sizeof(tracey_args_t));
    if (!args) return TRACEY_ARGS_ERROR_NOMEM;

    args->program_name = default_config.program_name;
    args->version_string = default_config.version_string;
    args->optimization = TRACEY_OPT_NONE;

    /* Initialize dynamic arrays */
    args->input_capacity = INITIAL_CAPACITY;
    args->include_capacity = INITIAL_CAPACITY;
    args->define_capacity = INITIAL_CAPACITY;

    args->input_files = calloc(args->input_capacity, sizeof(char*));
    args->include_paths = calloc(args->include_capacity, sizeof(char*));
    args->defines = calloc(args->define_capacity, sizeof(char*));

    if (!args->input_files || !args->include_paths || !args->defines) {
        tracey_args_free(args);
        return TRACEY_ARGS_ERROR_NOMEM;
    }

    /* Parse arguments */
    for (int i = 1; i < argc; i++) {
        const char* arg = argv[i];
        if (!arg) continue;

        if (strcmp(arg, "-h") == 0 || strcmp(arg, "--help") == 0) {
            args->help = true;
            return TRACEY_ARGS_HELP_REQUESTED;
        }
        else if (strcmp(arg, "--version") == 0) {
            args->version = true;
            return TRACEY_ARGS_VERSION_REQUESTED;
        }
        else if (strcmp(arg, "-v") == 0 || strcmp(arg, "--verbose") == 0) {
            args->verbose = true;
        }
        else if (strcmp(arg, "-Werror") == 0) {
            args->warnings_as_errors = true;
        }
        else if (strcmp(arg, "-g") == 0) {
            args->debug_info = true;
        }
        else if (strcmp(arg, "-o") == 0 || strcmp(arg, "--output") == 0) {
            const char* out_file = NULL;
            tracey_args_result_t res = parse_option_with_arg(&i, argc, argv, &out_file);
            if (res != TRACEY_ARGS_OK) {
                tracey_args_free(args);
                return res;
            }
            args->output_file = (char*)out_file;
        }
        else if (strcmp(arg, "-I") == 0 || strcmp(arg, "--include") == 0) {
            const char* path = NULL;
            tracey_args_result_t res = parse_option_with_arg(&i, argc, argv, &path);
            if (res != TRACEY_ARGS_OK) {
                tracey_args_free(args);
                return res;
            }
            if (args_add_string(&args->include_paths, &args->include_count, &args->include_capacity, path) != TRACEY_ARGS_OK) {
                tracey_args_free(args);
                return TRACEY_ARGS_ERROR_NOMEM;
            }
        }
        else if (strcmp(arg, "-D") == 0 || strcmp(arg, "--define") == 0) {
            const char* def = NULL;
            tracey_args_result_t res = parse_option_with_arg(&i, argc, argv, &def);
            if (res != TRACEY_ARGS_OK) {
                tracey_args_free(args);
                return res;
            }
            if (args_add_string(&args->defines, &args->define_count, &args->define_capacity, def) != TRACEY_ARGS_OK) {
                tracey_args_free(args);
                return TRACEY_ARGS_ERROR_NOMEM;
            }
        }
        else if (strncmp(arg, "-O", 2) == 0) {
            tracey_args_result_t res = parse_optimization(arg, &i, argc, argv, &args->optimization);
            if (res != TRACEY_ARGS_OK) {
                tracey_args_free(args);
                return res;
            }
        }
        else if (strncmp(arg, "--optimize", 10) == 0) {
            if (strcmp(arg, "--optimize") == 0) {
                const char* level = NULL;
                tracey_args_result_t res = parse_option_with_arg(&i, argc, argv, &level);
                if (res != TRACEY_ARGS_OK) {
                    tracey_args_free(args);
                    return res;
                }
                if (strlen(level) != 1 || level[0] < '0' || level[0] > '3') {
                    tracey_args_free(args);
                    return TRACEY_ARGS_ERROR_INVALID;
                }
                args->optimization = (tracey_opt_level_t)(level[0] - '0');
            }
            else if (strncmp(arg, "--optimize=", 11) == 0) {
                const char* level = arg + 11;
                if (strlen(level) != 1 || level[0] < '0' || level[0] > '3') {
                    tracey_args_free(args);
                    return TRACEY_ARGS_ERROR_INVALID;
                }
                args->optimization = (tracey_opt_level_t)(level[0] - '0');
            }
            else if (default_config.allow_unknown_options) {
                /* Unknown --optimize* variant */
            }
            else {
                tracey_args_free(args);
                return TRACEY_ARGS_ERROR_INVALID;
            }
        }
        else if (arg[0] == '-') {
            if (default_config.allow_unknown_options) {
                /* Silently ignore unknown options if configured */
            }
            else {
                tracey_args_free(args);
                return TRACEY_ARGS_ERROR_INVALID;
            }
        }
        else {
            /* Positional argument - input file */
            if (arg[0] == '\0') {
                tracey_args_free(args);
                return TRACEY_ARGS_ERROR_EMPTY_ARG;
            }
            if (args_add_string(&args->input_files, &args->input_count, &args->input_capacity, arg) != TRACEY_ARGS_OK) {
                tracey_args_free(args);
                return TRACEY_ARGS_ERROR_NOMEM;
            }
        }
    }

    *out_args = args;
    return TRACEY_ARGS_OK;
}

/* Parse -O<level> or -O <level> */
static tracey_args_result_t parse_optimization(const char* arg, int* i, int argc, char** argv, tracey_opt_level_t* out)
{
    const char* level_str = NULL;
    if (arg[2] != '\0') {
        level_str = arg + 2;
    } else {
        if (*i + 1 >= argc) return TRACEY_ARGS_ERROR_MISSING_ARG;
        level_str = argv[++(*i)];
    }
    if (strlen(level_str) != 1 || level_str[0] < '0' || level_str[0] > '3') {
        return TRACEY_ARGS_ERROR_INVALID;
    }
    *out = (tracey_opt_level_t)(level_str[0] - '0');
    return TRACEY_ARGS_OK;
}

/* Parse option that requires an argument (e.g., -o file, -I path) */
static tracey_args_result_t parse_option_with_arg(int* i, int argc, char** argv, const char** out)
{
    if (*i + 1 >= argc) return TRACEY_ARGS_ERROR_MISSING_ARG;
    const char* val = argv[++(*i)];
    if (val[0] == '\0') return TRACEY_ARGS_ERROR_EMPTY_ARG;
    *out = val;
    return TRACEY_ARGS_OK;
}