#include "tracey/source.h"

#include "source_internal.h"

#include <stdio.h>
#include <stdlib.h>

/*
 * tracey_source_init_empty - initialize a source snapshot to empty state.
 *
 * src must be non-NULL. No memory is allocated.
 */
void tracey_source_init_empty(struct tracey_source *src)
{
    src->path = NULL;
    src->content = NULL;
    src->size = 0;
}

/*
 * tracey_source_error_string - return a human-readable error message.
 */
const char *tracey_source_error_string(enum tracey_source_error err)
{
    switch (err)
    {
        case TRACEY_SOURCE_OK:
            return "No error";
        case TRACEY_SOURCE_INVALID_PATH:
            return "Invalid source file path";
        case TRACEY_SOURCE_OPEN_FAILED:
            return "Failed to open source file";
        case TRACEY_SOURCE_NOT_REGULAR_FILE:
            return "Source path is not a regular file";
        case TRACEY_SOURCE_TOO_LARGE:
            return "Source file exceeds the configured maximum size";
        case TRACEY_SOURCE_READ_FAILED:
            return "Failed to read source file contents";
        case TRACEY_SOURCE_NO_MEMORY:
            return "Out of memory while reading source file";
        default:
            return "Unknown source file error";
    }
}

/*
 * tracey_source_free - release memory owned by a source snapshot.
 */
void tracey_source_free(struct tracey_source *src)
{
    if (src == NULL)
        return;
    free(src->path);
    free(src->content);
    tracey_source_init_empty(src);
}

/*
 * tracey_source_print_debug - print source debug information.
 */
void tracey_source_print_debug(const struct tracey_source *src, FILE *out)
{
    if (src == NULL || out == NULL)
        return;
    fprintf(out, "Source path: %s\n", src->path ? src->path : "(null)");
    fprintf(out, "Source size: %zu bytes\n", src->size);
}
