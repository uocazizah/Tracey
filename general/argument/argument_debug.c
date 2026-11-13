#include "tracey/argument.h"

static void tracey_print_string_list(FILE *out, char **items, size_t count)
{
    size_t i;

    fprintf(out, "[");
    for (i = 0; i < count; i++)
    {
        fprintf(out, "%s", items[i] ? items[i] : "(null)");
        if (i + 1 < count)
            fprintf(out, ", ");
    }
    fprintf(out, "]");
}

void tracey_args_print_debug(const struct tracey_args *args, FILE *out)
{
    if (out == NULL)
        out = stdout;
    if (args == NULL)
    {
        fprintf(out, "Arguments: (null)\n");
        return;
    }

    fprintf(out, "Arguments:\n");
    fprintf(out, "  Program: %s\n", args->program_name ? args->program_name : "(null)");
    fprintf(out, "  Version: %s\n", args->version_string ? args->version_string : "(null)");
    fprintf(out, "  Input files: ");
    tracey_print_string_list(out, args->inputs, args->input_count);
    fprintf(out, " (count=%zu)\n", args->input_count);
    fprintf(out, "  Output file: %s\n", args->output ? args->output : "(none)");
    fprintf(out, "  Include paths: ");
    tracey_print_string_list(out, args->include_paths, args->include_path_count);
    fprintf(out, " (count=%zu)\n", args->include_path_count);
    fprintf(out, "  Defines: ");
    tracey_print_string_list(out, args->defines, args->define_count);
    fprintf(out, " (count=%zu)\n", args->define_count);
    fprintf(out, "  Optimization level: %d\n", (int)args->optimization);
    fprintf(out, "  Warnings as errors: %s\n", args->werror ? "true" : "false");
    fprintf(out, "  Debug info: %s\n", args->debug_info ? "true" : "false");
    fprintf(out, "  Verbose: %s\n", args->verbose ? "true" : "false");
    fprintf(out, "  Help: %s\n", args->help ? "true" : "false");
    fprintf(out, "  Version: %s\n", args->version ? "true" : "false");
}
