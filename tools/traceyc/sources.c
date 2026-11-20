#include "traceyc_internal.h"

#include "tracey/common.h"
#include "tracey/source.h"

#include <stdio.h>
#include <stdlib.h>

/*
 * tracey_load_sources - read every input file into a heap-allocated array.
 *
 * args supplies the input file list. On success the returned array contains
 * one initialized source per input and is owned by the caller. On failure any
 * partially loaded sources are released and NULL is returned.
 */
struct tracey_source *tracey_load_sources(const struct tracey_args *args)
{
    struct tracey_source *sources;
    size_t i;

    sources = (struct tracey_source *)calloc(args->input_count, sizeof(*sources));
    if (sources == NULL)
    {
        fprintf(stderr, "Error: out of memory while loading input files\n");
        return NULL;
    }

    for (i = 0; i < args->input_count; i++)
    {
        enum tracey_source_error err = tracey_source_read(args->inputs[i], &sources[i]);

        if (err != TRACEY_SOURCE_OK)
        {
            fprintf(stderr, "Error: failed to read input file '%s': %s\n", args->inputs[i],
                    tracey_source_error_string(err));
            fprintf(stderr,
                    "\033[1;34mnote:\033[0m check file existence, permissions, and that it is a "
                    "regular file\n");
            while (i > 0)
                tracey_source_free(&sources[--i]);
            free(sources);
            return NULL;
        }
    }

    return sources;
}