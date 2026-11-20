#include "tracey/argument.h"
#include "tracey/common.h"
#include "tracey/config.h"
#include "tracey/source.h"

#include "traceyc_internal.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(int argc, char **argv)
{
    struct tracey_args_config config;
    struct tracey_args args;
    enum tracey_args_error parse_error;
    struct tracey_source *sources;
    struct tracey_string generated;
    bool had_error = false;
    bool verbose;
    size_t i;

    memset(&config, 0, sizeof(config));
    config.program_name = "traceyc";
    config.version_string = TRACEY_VERSION_STRING;
    config.description = "Tracey compiler - compile .trc files";
    config.allow_unknown_options = false;
    config.max_args = 10000;

    tracey_args_init(&args);
    parse_error = tracey_args_parse(argc, argv, &config, &args);
    if (parse_error != TRACEY_ARGS_OK)
    {
        switch (parse_error)
        {
            case TRACEY_ARGS_NOMEM:
                fprintf(stderr, "Error: out of memory while parsing command-line arguments\n");
                fprintf(stderr,
                        "\033[1;34mnote:\033[0m reduce the number of arguments or free memory\n");
                break;
            case TRACEY_ARGS_TOO_MANY:
                fprintf(stderr, "Error: too many command-line arguments (max %zu)\n",
                        config.max_args);
                fprintf(stderr,
                        "\033[1;34mnote:\033[0m split the invocation or reduce input files\n");
                break;
            case TRACEY_ARGS_MISSING_ARG:
                fprintf(stderr, "Error: missing required command-line argument\n");
                fprintf(stderr, "\033[1;34mnote:\033[0m provide the missing option value\n");
                break;
            case TRACEY_ARGS_EMPTY_ARG:
                fprintf(stderr, "Error: empty command-line argument is not allowed\n");
                fprintf(stderr, "\033[1;34mnote:\033[0m provide a non-empty value\n");
                break;
            case TRACEY_ARGS_INVALID:
            default:
                fprintf(stderr, "Error: %s\n", tracey_args_error_string(parse_error));
                fprintf(stderr, "\033[1;34mnote:\033[0m review the command-line syntax\n");
                tracey_args_print_help(&config, stderr);
                break;
        }
        tracey_args_free(&args);
        return 1;
    }

    verbose = args.verbose;

    if (args.help)
    {
        tracey_args_print_help(&config, stdout);
        tracey_args_free(&args);
        return 0;
    }
    if (args.version)
    {
        tracey_args_print_version(&config, stdout);
        tracey_args_free(&args);
        return 0;
    }

    if (args.input_count == 0)
    {
        fprintf(stderr, "traceyc: \033[1;31mfatal error:\033[0m no input files provided\n");
        fprintf(stderr, "traceyc: \033[1;34mnote:\033[0m pass at least one .trc source file\n");
        tracey_args_free(&args);
        return 2;
    }

    sources = tracey_load_sources(&args);
    if (sources == NULL)
    {
        tracey_args_free(&args);
        return 1;
    }

    if (verbose)
    {
        tracey_args_print_debug(&args, stderr);
        for (i = 0; i < args.input_count; i++)
        {
            if (i > 0)
                fprintf(stderr, "\n");
            tracey_source_print_debug(&sources[i], stderr);
        }
    }

    had_error = tracey_compile_inputs(&args, sources, &generated);
    /* sources is released by tracey_compile_inputs */

    if (had_error)
    {
        tracey_string_free(&generated);
        tracey_args_free(&args);
        return 1;
    }

    /*
     * -S stops after code generation and emits assembly text either to the
     * requested output file or to standard output.
     */
    if (args.assembly)
    {
        int rc = tracey_emit_assembly_output(&args, &generated);
        tracey_args_free(&args);
        return rc;
    }

    {
        int rc = tracey_build_executable(&args, &generated);
        tracey_args_free(&args);
        return rc;
    }
}