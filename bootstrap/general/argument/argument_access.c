#include <tracey/argument.h>
#include "argument_internal.h"
#include <string.h>

/* Upper bound for a define name looked up via tracey_args_find_define().
 * Used to keep the NUL-terminator search bounded (CWE-126): input that is
 * not terminated within this limit is rejected instead of being scanned. */
#define TRACEY_ARGS_MAX_NAME_LEN 4096

/* Accessor implementations */
bool tracey_args_has_help(const tracey_args_t* args) { return args ? args->help : false; }
bool tracey_args_has_version(const tracey_args_t* args) { return args ? args->version : false; }
bool tracey_args_has_verbose(const tracey_args_t* args) { return args ? args->verbose : false; }
bool tracey_args_has_warnings_as_errors(const tracey_args_t* args) { return args ? args->warnings_as_errors : false; }
bool tracey_args_has_debug_info(const tracey_args_t* args) { return args ? args->debug_info : false; }

tracey_opt_level_t tracey_args_get_optimization(const tracey_args_t* args) { return args ? args->optimization : TRACEY_OPT_NONE; }
const char* tracey_args_get_output(const tracey_args_t* args) { return args ? args->output_file : NULL; }

size_t tracey_args_input_count(const tracey_args_t* args) { return args ? args->input_count : 0; }
const char* tracey_args_get_input(const tracey_args_t* args, size_t index)
{
    return (args && index < args->input_count) ? args->input_files[index] : NULL;
}

size_t tracey_args_include_count(const tracey_args_t* args) { return args ? args->include_count : 0; }
const char* tracey_args_get_include(const tracey_args_t* args, size_t index)
{
    return (args && index < args->include_count) ? args->include_paths[index] : NULL;
}

size_t tracey_args_define_count(const tracey_args_t* args) { return args ? args->define_count : 0; }
const char* tracey_args_get_define(const tracey_args_t* args, size_t index)
{
    return (args && index < args->define_count) ? args->defines[index] : NULL;
}

const char* tracey_args_find_define(const tracey_args_t* args, const char* name)
{
    size_t name_len;
    size_t i;
    const char* nul;

    if (!args || !name) return NULL;
    /* Bounded terminator search instead of an unbounded scan: memchr stops
     * at the first NUL or the bound, so a non-terminating "name" can over-read
     * at most TRACEY_ARGS_MAX_NAME_LEN bytes before being rejected (CWE-126). */
    nul = (const char*)memchr(name, '\0', TRACEY_ARGS_MAX_NAME_LEN);
    if (!nul) return NULL;
    name_len = (size_t)(nul - name);
    for (i = 0; i < args->define_count; i++) {
        const char* def = args->defines[i];
        if (strncmp(def, name, name_len) == 0 && (def[name_len] == '\0' || def[name_len] == '=')) {
            return def;
        }
    }
    return NULL;
}

bool tracey_args_has_include_path(const tracey_args_t* args, const char* path)
{
    size_t i;
    if (!args || !path) return false;
    for (i = 0; i < args->include_count; i++) {
        if (strcmp(args->include_paths[i], path) == 0) return true;
    }
    return false;
}
