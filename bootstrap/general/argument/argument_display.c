#include <tracey/argument.h>
#include <stdio.h>

void arguments_print_help(const char* program_name) {
    printf("Usage: %s [options] [input_files...]\n", program_name ? program_name : "program");
    printf("Options:\n");
    printf("  -h, --help                 Show this help message\n");
    printf("  --version                  Show version information\n");
    printf("  -v, --verbose              Enable verbose output\n");
    printf("  -o, --output <file>        Specify output file\n");
    printf("  -I, --include <path>       Add include path (can be repeated)\n");
    printf("  -D, --define <macro>       Define a macro (can be repeated)\n");
    printf("  -O<level>                  Set optimization level (0-3)\n");
    printf("  --optimize <level>         Set optimization level (0-3)\n");
    printf("  -Werror                    Treat warnings as errors\n");
    printf("  -g                         Generate debug information\n");
}

void arguments_print_version(const char* program_name, const char* version_string) {
    printf("%s version %s\n", program_name ? program_name : "program",
           version_string ? version_string : "unknown");
}

void arguments_print_debug(const Arguments* args) {
    if (!args) {
        printf("Arguments: NULL\n");
        return;
    }

    printf("Arguments:\n");
    printf("  Input files: ");
    if (args->input_file_count > 0) {
        printf("[");
        for (size_t i = 0; i < args->input_file_count; i++) {
            printf("%s%s", args->input_files[i],
                   (i + 1 < args->input_file_count) ? ", " : "");
        }
        printf("] (count=%zu)\n", args->input_file_count);
    } else {
        printf("[] (count=0)\n");
    }

    printf("  Output file: %s\n", args->output_file ? args->output_file : "(none)");

    printf("  Include paths: ");
    if (args->include_path_count > 0) {
        printf("[");
        for (size_t i = 0; i < args->include_path_count; i++) {
            printf("%s%s", args->include_paths[i],
                   (i + 1 < args->include_path_count) ? ", " : "");
        }
        printf("] (count=%zu)\n", args->include_path_count);
    } else {
        printf("[] (count=0)\n");
    }

    printf("  Defines: ");
    if (args->define_count > 0) {
        printf("[");
        for (size_t i = 0; i < args->define_count; i++) {
            printf("%s%s", args->defines[i],
                   (i + 1 < args->define_count) ? ", " : "");
        }
        printf("] (count=%zu)\n", args->define_count);
    } else {
        printf("[] (count=0)\n");
    }

    printf("  Optimization level: %d\n", args->optimization_level);
    printf("  Warnings as errors: %s\n", args->warnings_as_errors ? "true" : "false");
    printf("  Debug info: %s\n", args->debug_info ? "true" : "false");
    printf("  Verbose: %s\n", args->verbose ? "true" : "false");
    printf("  Help: %s\n", args->help ? "true" : "false");
    printf("  Version: %s\n", args->version ? "true" : "false");
}
