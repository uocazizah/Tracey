#include "traceyc_internal.h"

#include "tracey/argument.h"
#include "tracey/common.h"
#include "tracey/config.h"
#include "tracey/pipeline.h"
#include "tracey/source.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*
 * tracey_compile_inputs - preprocess, lex, parse, analyze, and generate code.
 *
 * args supplies the parsed command-line options. sources is the array of
 * already-loaded input files. generated receives the concatenated assembly
 * text on return. The input sources and include-path storage are released
 * before this function returns.
 *
 * Returns true when at least one input reported an error.
 */
bool tracey_compile_inputs(const struct tracey_args *args, struct tracey_source *sources,
                           struct tracey_string *generated)
{
    const char *const *include_paths;
    char **owned_paths;
    size_t include_count;
    struct tracey_compile_options opts;
    struct tracey_compile_pipeline pipeline;
    bool had_error = false;
    bool verbose = args->verbose;
    size_t i;

    include_paths = tracey_collect_include_paths(args, &include_count, &owned_paths);
    if (include_paths == NULL)
    {
        fprintf(stderr, "Error: out of memory while building include paths\n");
        for (i = 0; i < args->input_count; i++)
            tracey_source_free(&sources[i]);
        free(sources);
        return true;
    }

    memset(&opts, 0, sizeof(opts));
    opts.arch = TRACEY_TARGET_AARCH64;
    opts.verbose = verbose;
    opts.werror = args->werror;
    opts.include_paths = include_paths;
    opts.include_path_count = include_count;

    tracey_pipeline_init(&pipeline, &opts);
    tracey_string_init(generated);

    for (i = 0; i < args->input_count; i++)
    {
        bool emit_runtime = (i + 1 == args->input_count);
        struct tracey_file_result result =
            tracey_pipeline_process_file(&pipeline, &sources[i], args->inputs[i], i, emit_runtime);

        if (result.had_error)
        {
            had_error = true;
            free(result.assembly);
            continue;
        }
        if (result.assembly != NULL)
        {
            tracey_string_append_cstr(generated, result.assembly);
            free(result.assembly);
        }
    }

    for (i = 0; i < args->input_count; i++)
        tracey_source_free(&sources[i]);
    free(sources);

    for (i = 0; i < include_count; i++)
        free(owned_paths[i]);
    free(owned_paths);
    free((void *)include_paths);

    return had_error;
}