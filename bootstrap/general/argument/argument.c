#include <tracey/argument.h>
#include "argument_internal.h"
#include <stdlib.h>
#include <string.h>

void tracey_args_free(tracey_args_t* args)
{
    if (!args) return;

    /* output_file points to argv, don't free it */
    args_free_array(args->input_files);
    args_free_array(args->include_paths);
    args_free_array(args->defines);

    /* Clear sensitive data */
    memset(args, 0, sizeof(tracey_args_t));
    free(args);
}

const char* tracey_args_strerror(tracey_args_result_t result)
{
    switch (result) {
        case TRACEY_ARGS_OK: return "Success";
        case TRACEY_ARGS_ERROR_NOMEM: return "Out of memory";
        case TRACEY_ARGS_ERROR_INVALID: return "Invalid argument";
        case TRACEY_ARGS_ERROR_TOO_MANY: return "Too many arguments";
        case TRACEY_ARGS_ERROR_MISSING_ARG: return "Missing required argument";
        case TRACEY_ARGS_ERROR_EMPTY_ARG: return "Empty argument not allowed";
        case TRACEY_ARGS_HELP_REQUESTED: return "Help requested";
        case TRACEY_ARGS_VERSION_REQUESTED: return "Version requested";
        default: return "Unknown error";
    }
}