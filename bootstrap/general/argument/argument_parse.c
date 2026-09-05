#include <tracey/argument.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

Arguments* arguments_parse(int argc, char** argv) {
    if (argc <= 0 || argv == NULL) return NULL;

    Arguments* args = calloc(1, sizeof(Arguments));
    if (!args) return NULL;

    // Alokasikan array dengan kapasitas maksimum argc
    args->input_files = malloc((size_t)argc * sizeof(char*));
    args->include_paths = malloc((size_t)argc * sizeof(char*));
    args->defines = malloc((size_t)argc * sizeof(char*));
    if (!args->input_files || !args->include_paths || !args->defines) {
        arguments_free(args);
        return NULL;
    }

    // Inisialisasi nilai default
    args->input_file_count = 0;
    args->include_path_count = 0;
    args->define_count = 0;
    args->output_file = NULL;
    args->optimization_level = 0;
    args->warnings_as_errors = false;
    args->debug_info = false;
    args->verbose = false;
    args->help = false;
    args->version = false;

    for (int i = 1; i < argc; i++) {
        char* arg = argv[i];

        if (strcmp(arg, "-h") == 0 || strcmp(arg, "--help") == 0) {
            args->help = true;
        } else if (strcmp(arg, "-v") == 0 || strcmp(arg, "--verbose") == 0) {
            args->verbose = true;
        } else if (strcmp(arg, "--version") == 0) {
            args->version = true;
        } else if (strcmp(arg, "-Werror") == 0) {
            args->warnings_as_errors = true;
        } else if (strcmp(arg, "-g") == 0) {
            args->debug_info = true;
        } else if (strcmp(arg, "-o") == 0 || strcmp(arg, "--output") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "Option '%s' requires an argument\n", arg);
                arguments_free(args);
                return NULL;
            }
            args->output_file = argv[++i];
        } else if (strcmp(arg, "-I") == 0 || strcmp(arg, "--include") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "Option '%s' requires an argument\n", arg);
                arguments_free(args);
                return NULL;
            }
            args->include_paths[args->include_path_count++] = argv[++i];
        } else if (strcmp(arg, "-D") == 0 || strcmp(arg, "--define") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "Option '%s' requires an argument\n", arg);
                arguments_free(args);
                return NULL;
            }
            args->defines[args->define_count++] = argv[++i];
        } else if (strncmp(arg, "-O", 2) == 0) {
            // Format: -O<level> atau -O <level>
            const char* level_str = NULL;
            if (arg[2] != '\0') {
                level_str = arg + 2;
            } else {
                if (i + 1 >= argc) {
                    fprintf(stderr, "Option '-O' requires an argument\n");
                    arguments_free(args);
                    return NULL;
                }
                level_str = argv[++i];
            }
            if (strlen(level_str) != 1 || level_str[0] < '0' || level_str[0] > '3') {
                fprintf(stderr, "Invalid optimization level: '%s' (expected 0-3)\n", level_str);
                arguments_free(args);
                return NULL;
            }
            args->optimization_level = level_str[0] - '0';
        } else if (strncmp(arg, "--optimize=", 11) == 0) {
            const char* level_str = arg + 11;
            if (strlen(level_str) != 1 || level_str[0] < '0' || level_str[0] > '3') {
                fprintf(stderr, "Invalid optimization level: '%s' (expected 0-3)\n", level_str);
                arguments_free(args);
                return NULL;
            }
            args->optimization_level = level_str[0] - '0';
        } else if (strcmp(arg, "--optimize") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "Option '--optimize' requires an argument\n");
                arguments_free(args);
                return NULL;
            }
            const char* level_str = argv[++i];
            if (strlen(level_str) != 1 || level_str[0] < '0' || level_str[0] > '3') {
                fprintf(stderr, "Invalid optimization level: '%s' (expected 0-3)\n", level_str);
                arguments_free(args);
                return NULL;
            }
            args->optimization_level = level_str[0] - '0';
        } else if (arg[0] == '-') {
            fprintf(stderr, "Unknown option: '%s'\n", arg);
            arguments_free(args);
            return NULL;
        } else {
            // Input file
            args->input_files[args->input_file_count++] = arg;
        }
    }

    // Kecilkan array agar sesuai dengan jumlah elemen aktual
    if (args->input_file_count > 0) {
        char** tmp = realloc(args->input_files, args->input_file_count * sizeof(char*));
        if (tmp) args->input_files = tmp;
    } else {
        free(args->input_files);
        args->input_files = NULL;
    }

    if (args->include_path_count > 0) {
        char** tmp = realloc(args->include_paths, args->include_path_count * sizeof(char*));
        if (tmp) args->include_paths = tmp;
    } else {
        free(args->include_paths);
        args->include_paths = NULL;
    }

    if (args->define_count > 0) {
        char** tmp = realloc(args->defines, args->define_count * sizeof(char*));
        if (tmp) args->defines = tmp;
    } else {
        free(args->defines);
        args->defines = NULL;
    }

    return args;
}

void arguments_free(Arguments* args) {
    if (!args) return;
    free(args->input_files);
    free(args->include_paths);
    free(args->defines);
    free(args);
}
