#include <tracey/argument.h>
#include <tracey/source.h>
#include <stdio.h>
#include <stdlib.h>

int main(int argc, char** argv)
{
    tracey_args_t* args = NULL;
    tracey_args_config_t config;
    tracey_args_result_t result;
    size_t input_count = 0;
    tracey_source_t** sources = NULL;
    size_t i = 0;

    config.program_name = "traceyc";
    config.version_string = "1.0.0";
    config.description = "Tracey compiler - compile .trc files";
    config.allow_unknown_options = false;
    config.max_args = 10000;

    result = tracey_args_parse(&args, argc, argv, &config);

    switch (result) {
        case TRACEY_ARGS_HELP_REQUESTED:
            tracey_args_print_help(&config, stdout);
            tracey_args_free(args);
            return 0;

        case TRACEY_ARGS_VERSION_REQUESTED:
            tracey_args_print_version(&config, stdout);
            tracey_args_free(args);
            return 0;

        case TRACEY_ARGS_OK:
            /* Continue processing */
            break;

        case TRACEY_ARGS_ERROR_NOMEM:
            fprintf(stderr, "Error: Out of memory\n");
            return 1;

        case TRACEY_ARGS_ERROR_TOO_MANY:
            fprintf(stderr, "Error: Too many arguments (max %zu)\n", config.max_args);
            return 1;

        case TRACEY_ARGS_ERROR_MISSING_ARG:
            fprintf(stderr, "Error: Missing required argument\n");
            return 1;

        case TRACEY_ARGS_ERROR_EMPTY_ARG:
            fprintf(stderr, "Error: Empty argument not allowed\n");
            return 1;

        case TRACEY_ARGS_ERROR_INVALID:
        default:
            fprintf(stderr, "Error: %s\n", tracey_args_strerror(result));
            tracey_args_print_help(&config, stderr);
            if (args) tracey_args_free(args);
            return 1;
    }

    /* Read input files */
    input_count = tracey_args_input_count(args);

    if (input_count > 0) {
        sources = calloc(input_count, sizeof(tracey_source_t*));
        if (!sources) {
            fprintf(stderr, "Error: Out of memory\n");
            tracey_args_free(args);
            return 1;
        }

        for (i = 0; i < input_count; i++) {
            const char* path = tracey_args_get_input(args, i);
            sources[i] = tracey_source_read(path);
            if (!sources[i]) {
                fprintf(stderr, "Error: Failed to read file '%s'\n", path);
                for (size_t j = 0; j < i; j++) {
                    tracey_source_free(sources[j]);
                }
                free(sources);
                tracey_args_free(args);
                return 1;
            }
        }
    }

    /* Debug output */
    if (tracey_args_has_verbose(args)) {
        tracey_args_print_debug(args, stdout);

        for (i = 0; i < input_count; i++) {
            if (i > 0) fprintf(stdout, "\n");
            tracey_source_print_debug(sources[i], stdout);
        }
    }

    /* Cleanup */
    for (i = 0; i < input_count; i++) {
        tracey_source_free(sources[i]);
    }
    free(sources);
    tracey_args_free(args);

    return 0;
}