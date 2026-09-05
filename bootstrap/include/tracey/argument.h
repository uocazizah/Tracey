#ifndef ARGUMENT_H
#define ARGUMENT_H

#include <stdbool.h>
#include <stddef.h>

typedef struct {
    char** input_files;
    size_t input_file_count;
    char* output_file;
    char** include_paths;
    size_t include_path_count;
    char** defines;
    size_t define_count;
    int optimization_level;
    bool warnings_as_errors;
    bool debug_info;
    bool verbose;
    bool help;
    bool version;
    char _padding[7];
} Arguments;

Arguments* arguments_parse(int argc, char** argv);
void arguments_free(Arguments* args);

bool arguments_has_input_files(const Arguments* args);
const char* arguments_get_output_file(const Arguments* args);
int arguments_get_optimization_level(const Arguments* args);
bool arguments_get_warnings_as_errors(const Arguments* args);
bool arguments_get_debug_info(const Arguments* args);
bool arguments_get_verbose(const Arguments* args);
bool arguments_get_help(const Arguments* args);
bool arguments_get_version(const Arguments* args);

const char* arguments_find_define(const Arguments* args, const char* name);
bool arguments_has_include_path(const Arguments* args, const char* path);

void arguments_print_help(const char* program_name);
void arguments_print_version(const char* program_name, const char* version_string);
void arguments_print_debug(const Arguments* args);

#endif /* ARGUMENT_H */
