#include <tracey/argument.h>
#include "argument_internal.h"
#include <string.h>

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
    if (!args || !name) return NULL;
    size_t name_len = strlen(name);
    for (size_t i = 0; i < args->define_count; i++) {
        const char* def = args->defines[i];
        if (strncmp(def, name, name_len) == 0 && (def[name_len] == '\0' || def[name_len] == '=')) {
            return def;
        }
    }
    return NULL;
}

bool tracey_args_has_include_path(const tracey_args_t* args, const char* path)
{
    if (!args || !path) return false;
    for (size_t i = 0; i < args->include_count; i++) {
        if (strcmp(args->include_paths[i], path) == 0) return true;
    }
    return false;
}