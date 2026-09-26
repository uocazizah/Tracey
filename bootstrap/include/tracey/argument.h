#ifndef TRACEY_ARGUMENT_H
#define TRACEY_ARGUMENT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Opaque handle for argument parser state */
typedef struct tracey_args tracey_args_t;

/* Parse result codes */
typedef enum {
    TRACEY_ARGS_OK = 0,
    TRACEY_ARGS_ERROR_NOMEM = -1,
    TRACEY_ARGS_ERROR_INVALID = -2,
    TRACEY_ARGS_ERROR_TOO_MANY = -3,
    TRACEY_ARGS_ERROR_MISSING_ARG = -4,
    TRACEY_ARGS_ERROR_EMPTY_ARG = -5,
    TRACEY_ARGS_HELP_REQUESTED = 1,
    TRACEY_ARGS_VERSION_REQUESTED = 2,
} tracey_args_result_t;

/* Optimization levels */
typedef enum {
    TRACEY_OPT_NONE = 0,
    TRACEY_OPT_LEVEL1 = 1,
    TRACEY_OPT_LEVEL2 = 2,
    TRACEY_OPT_LEVEL3 = 3,
} tracey_opt_level_t;

/* Argument parser configuration */
typedef struct {
    const char* program_name;
    const char* version_string;
    const char* description;
    size_t max_args;
    bool allow_unknown_options;
} tracey_args_config_t;

/* Parse command line arguments */
tracey_args_result_t tracey_args_parse(tracey_args_t** out_args,
                                       int argc, char** argv,
                                       const tracey_args_config_t* config);

/* Free parsed arguments */
void tracey_args_free(tracey_args_t* args);

/* Accessor functions */
bool tracey_args_has_help(const tracey_args_t* args);
bool tracey_args_has_version(const tracey_args_t* args);
bool tracey_args_has_verbose(const tracey_args_t* args);
bool tracey_args_has_warnings_as_errors(const tracey_args_t* args);
bool tracey_args_has_debug_info(const tracey_args_t* args);

tracey_opt_level_t tracey_args_get_optimization(const tracey_args_t* args);
const char* tracey_args_get_output(const tracey_args_t* args);
size_t tracey_args_input_count(const tracey_args_t* args);
const char* tracey_args_get_input(const tracey_args_t* args, size_t index);
size_t tracey_args_include_count(const tracey_args_t* args);
const char* tracey_args_get_include(const tracey_args_t* args, size_t index);
size_t tracey_args_define_count(const tracey_args_t* args);
const char* tracey_args_get_define(const tracey_args_t* args, size_t index);
const char* tracey_args_find_define(const tracey_args_t* args, const char* name);
bool tracey_args_has_include_path(const tracey_args_t* args, const char* path);

/* Help and version printing */
void tracey_args_print_help(const tracey_args_config_t* config, FILE* out);
void tracey_args_print_version(const tracey_args_config_t* config, FILE* out);

/* Debug output */
void tracey_args_print_debug(const tracey_args_t* args, FILE* out);

/* Error string for result codes */
const char* tracey_args_strerror(tracey_args_result_t result);

#ifdef __cplusplus
}
#endif

#endif /* TRACEY_ARGUMENT_H */