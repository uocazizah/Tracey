#include <tracey/argument.h>

bool arguments_has_input_files(const Arguments* args) {
    return args && args->input_file_count > 0;
}

const char* arguments_get_output_file(const Arguments* args) {
    return args ? args->output_file : NULL;
}

int arguments_get_optimization_level(const Arguments* args) {
    return args ? args->optimization_level : 0;
}

bool arguments_get_warnings_as_errors(const Arguments* args) {
    return args && args->warnings_as_errors;
}

bool arguments_get_debug_info(const Arguments* args) {
    return args && args->debug_info;
}

bool arguments_get_verbose(const Arguments* args) {
    return args && args->verbose;
}

bool arguments_get_help(const Arguments* args) {
    return args && args->help;
}

bool arguments_get_version(const Arguments* args) {
    return args && args->version;
}
