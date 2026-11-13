#ifndef TRACEY_ARGUMENT_H
#define TRACEY_ARGUMENT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Optimization levels. */
enum tracey_opt_level
{
    TRACEY_OPT_NONE = 0,
    TRACEY_OPT_LEVEL1 = 1,
    TRACEY_OPT_LEVEL2 = 2,
    TRACEY_OPT_LEVEL3 = 3,
};

/* Argument parsing error codes. */
enum tracey_args_error
{
    TRACEY_ARGS_OK = 0,
    TRACEY_ARGS_NOMEM,
    TRACEY_ARGS_INVALID,
    TRACEY_ARGS_TOO_MANY,
    TRACEY_ARGS_MISSING_ARG,
    TRACEY_ARGS_EMPTY_ARG,
};

/* Configuration controlling argument parsing and help/version output. */
struct tracey_args_config
{
    const char *program_name;
    const char *version_string;
    const char *description;
    size_t max_args;
    bool allow_unknown_options;
};

/*
 * Parsed command-line arguments.
 *
 * All string arrays own heap-allocated NUL-terminated copies. output may
 * be NULL when no output file was specified. The structure must be
 * released with tracey_args_free.
 */
struct tracey_args
{
    char **inputs;
    size_t input_count;
    char **include_paths;
    size_t include_path_count;
    char **defines;
    size_t define_count;
    char *output;

    char *program_name;
    char *version_string;
    char *description;

    enum tracey_opt_level optimization;
    bool help;
    bool version;
    bool verbose;
    bool werror;
    bool debug_info;
    bool assembly;
};

/*
 * tracey_args_init - initialize an args structure to empty defaults.
 */
void tracey_args_init(struct tracey_args *args);

/*
 * tracey_args_free - release memory owned by parsed arguments.
 */
void tracey_args_free(struct tracey_args *args);

/*
 * tracey_args_parse - parse command-line arguments.
 *
 * argc and argv follow the conventional main argument contract. config
 * may be NULL to use defaults. On success, out is initialized with owned
 * data and TRACEY_ARGS_OK is returned. On failure, out is left in a safe
 * empty state and an error code is returned.
 */
enum tracey_args_error tracey_args_parse(int argc, char **argv, const struct tracey_args_config *config,
                                         struct tracey_args *out);

/*
 * tracey_args_find_define - find a macro definition by name.
 *
 * Returns a pointer to the stored define string ("NAME" or "NAME=value")
 * if present, or NULL if not found. The returned pointer is owned by args
 * and remains valid until args is freed or modified.
 */
const char *tracey_args_find_define(const struct tracey_args *args, const char *name);

/*
 * tracey_args_has_include_path - check whether an include path is present.
 */
bool tracey_args_has_include_path(const struct tracey_args *args, const char *path);

/*
 * tracey_args_error_string - return a human-readable parse error message.
 */
const char *tracey_args_error_string(enum tracey_args_error err);

/*
 * tracey_args_print_help - print usage information.
 */
void tracey_args_print_help(const struct tracey_args_config *config, FILE *out);

/*
 * tracey_args_print_version - print version and build information.
 */
void tracey_args_print_version(const struct tracey_args_config *config, FILE *out);

/*
 * tracey_args_print_debug - print parsed argument state.
 */
void tracey_args_print_debug(const struct tracey_args *args, FILE *out);

#ifdef __cplusplus
}
#endif

#endif /* TRACEY_ARGUMENT_H */
