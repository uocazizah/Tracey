#include <tracey/argument.h>
#include "argument_internal.h"
#include <stdio.h>

void tracey_args_print_debug(const tracey_args_t* args, FILE* out)
{
    if (!out) out = stdout;

    if (!args) {
        fprintf(out, "Arguments: NULL\n");
        return;
    }

    fprintf(out, "Arguments:\n");
    fprintf(out, "  Program: %s\n", args->program_name ? args->program_name : "(unknown)");
    fprintf(out, "  Version: %s\n", args->version_string ? args->version_string : "(unknown)");

    fprintf(out, "  Input files: ");
    if (args->input_count > 0) {
        fprintf(out, "[");
        for (size_t i = 0; i < args->input_count; i++) {
            fprintf(out, "%s%s", args->input_files[i], (i + 1 < args->input_count) ? ", " : "");
        }
        fprintf(out, "] (count=%zu)\n", args->input_count);
    } else {
        fprintf(out, "[] (count=0)\n");
    }

    fprintf(out, "  Output file: %s\n", args->output_file ? args->output_file : "(none)");

    fprintf(out, "  Include paths: ");
    if (args->include_count > 0) {
        fprintf(out, "[");
        for (size_t i = 0; i < args->include_count; i++) {
            fprintf(out, "%s%s", args->include_paths[i], (i + 1 < args->include_count) ? ", " : "");
        }
        fprintf(out, "] (count=%zu)\n", args->include_count);
    } else {
        fprintf(out, "[] (count=0)\n");
    }

    fprintf(out, "  Defines: ");
    if (args->define_count > 0) {
        fprintf(out, "[");
        for (size_t i = 0; i < args->define_count; i++) {
            fprintf(out, "%s%s", args->defines[i], (i + 1 < args->define_count) ? ", " : "");
        }
        fprintf(out, "] (count=%zu)\n", args->define_count);
    } else {
        fprintf(out, "[] (count=0)\n");
    }

    fprintf(out, "  Optimization level: %u\n", (unsigned int)args->optimization);
    fprintf(out, "  Warnings as errors: %s\n", args->warnings_as_errors ? "true" : "false");
    fprintf(out, "  Debug info: %s\n", args->debug_info ? "true" : "false");
    fprintf(out, "  Verbose: %s\n", args->verbose ? "true" : "false");
    fprintf(out, "  Help: %s\n", args->help ? "true" : "false");
    fprintf(out, "  Version: %s\n", args->version ? "true" : "false");
}