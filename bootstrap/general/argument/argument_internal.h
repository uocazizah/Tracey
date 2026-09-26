#ifndef TRACEY_ARGUMENT_INTERNAL_H
#define TRACEY_ARGUMENT_INTERNAL_H

#include <tracey/argument.h>
#include <stddef.h>

#define INITIAL_CAPACITY 16

/* Internal structure definition - only visible to argument implementation files */
struct tracey_args {
    /* Pointers first (8-byte aligned on 64-bit) */
    char** input_files;
    char** include_paths;
    char** defines;
    char* output_file;
    const char* program_name;
    const char* version_string;

    /* Size_t fields */
    size_t input_count;
    size_t input_capacity;
    size_t include_count;
    size_t include_capacity;
    size_t define_count;
    size_t define_capacity;

    /* Enums and bools */
    tracey_opt_level_t optimization;

    bool help;
    bool version;
    bool verbose;
    bool warnings_as_errors;
    bool debug_info;
};

/* Internal utility declarations */
tracey_args_result_t args_ensure_capacity(void*** array, size_t* count, size_t* capacity);
tracey_args_result_t args_add_string(char*** array, size_t* count, size_t* capacity, const char* str);
void args_free_array(char** array);

#endif /* TRACEY_ARGUMENT_INTERNAL_H */