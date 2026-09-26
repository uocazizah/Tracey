#include <tracey/argument.h>
#include "argument_internal.h"
#include <stdio.h>

void tracey_args_print_help(const tracey_args_config_t* config, FILE* out)
{
    if (!out) out = stdout;
    const char* prog = (config && config->program_name) ? config->program_name : "tracey";
    const char* desc = (config && config->description) ? config->description : "";

    fprintf(out, "Usage: %s [options] [input_files...]\n", prog);
    if (*desc) fprintf(out, "%s\n\n", desc);
    fprintf(out, "Options:\n");
    fprintf(out, "  -h, --help                 Show this help message\n");
    fprintf(out, "  --version                  Show version information\n");
    fprintf(out, "  -v, --verbose              Enable verbose output\n");
    fprintf(out, "  -o, --output <file>        Specify output file\n");
    fprintf(out, "  -I, --include <path>       Add include path (can be repeated)\n");
    fprintf(out, "  -D, --define <macro>       Define a macro (can be repeated)\n");
    fprintf(out, "  -O<level>                  Set optimization level (0-3)\n");
    fprintf(out, "  --optimize <level>         Set optimization level (0-3)\n");
    fprintf(out, "  -Werror                    Treat warnings as errors\n");
    fprintf(out, "  -g                         Generate debug information\n");
}

void tracey_args_print_version(const tracey_args_config_t* config, FILE* out)
{
    if (!out) out = stdout;
    const char* prog = (config && config->program_name) ? config->program_name : "tracey";
    const char* ver = (config && config->version_string) ? config->version_string : "unknown";
    fprintf(out, "%s version %s\n", prog, ver);
}