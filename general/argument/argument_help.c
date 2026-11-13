#include "tracey/argument.h"
#include "tracey/config.h"

void tracey_args_print_help(const struct tracey_args_config *config, FILE *out)
{
    const char *prog = "tracey";
    const char *desc = NULL;

    if (out == NULL)
        out = stdout;
    if (config != NULL)
    {
        if (config->program_name)
            prog = config->program_name;
        desc = config->description;
    }

    fprintf(out, "Usage: %s [options] [input_files...]\n", prog);
    if (desc && desc[0] != '\0')
        fprintf(out, "%s\n\n", desc);
    fprintf(out, "Options:\n");
    fprintf(out, "  -h, --help                 Show this help message\n");
    fprintf(out, "  -V, --verbose              Enable verbose stage logging\n");
    fprintf(out, "  -v, --version              Show detailed version information\n");
    fprintf(out, "  -o, --output <file>        Specify output file\n");
    fprintf(out, "  -I, --include <path>       Add include path (can be repeated)\n");
    fprintf(out, "  -D, --define <macro>       Define a macro (can be repeated)\n");
    fprintf(out, "  -O<level>                  Set optimization level (0-3)\n");
    fprintf(out, "  --optimize <level>         Set optimization level (0-3)\n");
    fprintf(out, "  -Werror                    Treat warnings as errors\n");
    fprintf(out, "  -g                         Generate debug information\n");
}

void tracey_args_print_version(const struct tracey_args_config *config, FILE *out)
{
    const char *prog = "tracey";
    const char *ver = TRACEY_VERSION;

    if (out == NULL)
        out = stdout;
    if (config != NULL)
    {
        if (config->program_name)
            prog = config->program_name;
        if (config->version_string)
            ver = config->version_string;
    }

    fprintf(out, "%s version %s\n", prog, ver);
    fprintf(out, "Target: %s\n", TRACEY_DEFAULT_TARGET);
    fprintf(out, "ABI: %d-bit\n", TRACEY_ABI_BITS);
    fprintf(out, "Toolchain: %s\n", __VERSION__);
    fprintf(out, "Built: %s %s\n", __DATE__, __TIME__);
    fprintf(out, "Build configuration:\n");
    fprintf(out, "  Debug trace: %s\n", TRACEY_DEBUG_TRACE ? "enabled" : "disabled");
    fprintf(out, "  zlib: %s\n", TRACEY_USE_ZLIB ? "enabled" : "disabled");
    fprintf(out, "  threads: %s\n", TRACEY_USE_THREADS ? "enabled" : "disabled");
    fprintf(out, "  max source size: %zu\n", TRACEY_MAX_SOURCE_SIZE);
    fprintf(out, "  stack size: %zu\n", TRACEY_STACK_SIZE);
    fprintf(out, "Modules:\n");
    fprintf(out, "  parser: %s\n", TRACEY_MODULE_PARSER ? "enabled" : "disabled");
    fprintf(out, "  semantic: %s\n", TRACEY_MODULE_SEMANTIC ? "enabled" : "disabled");
    fprintf(out, "  codegen: %s\n", TRACEY_MODULE_CODEGEN ? "enabled" : "disabled");
    fprintf(out, "  assembler: %s\n", TRACEY_MODULE_ASSEMBLER ? "enabled" : "disabled");
    fprintf(out, "  linker: %s\n", TRACEY_MODULE_LINKER ? "enabled" : "disabled");
}
