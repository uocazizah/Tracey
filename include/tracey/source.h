#ifndef TRACEY_SOURCE_H
#define TRACEY_SOURCE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Error codes for tracey_source_read. */
enum tracey_source_error
{
    TRACEY_SOURCE_OK = 0,
    TRACEY_SOURCE_INVALID_PATH,
    TRACEY_SOURCE_OPEN_FAILED,
    TRACEY_SOURCE_NOT_REGULAR_FILE,
    TRACEY_SOURCE_TOO_LARGE,
    TRACEY_SOURCE_READ_FAILED,
    TRACEY_SOURCE_NO_MEMORY,
};

/*
 * Owned, immutable snapshot of a source file loaded into memory.
 *
 * path points to a heap-allocated NUL-terminated copy of the path.
 * content points to a heap-allocated buffer of length size bytes and is
 * NUL-terminated for convenience. The caller owns both allocations and
 * must release them with tracey_source_free.
 */
struct tracey_source
{
    char *path;
    char *content;
    size_t size;
};

/*
 * tracey_source_read - read a source file from disk.
 *
 * path is the NUL-terminated path to the source file. On success, out is
 * initialized with an owned copy of the path and file contents and the
 * function returns TRACEY_SOURCE_OK. On failure, out is left in a safe
 * empty state and an error code is returned.
 *
 * The POSIX implementation uses O_NOFOLLOW and fstat checks to avoid
 * following symlinks and to validate that the input is a regular file.
 */
enum tracey_source_error tracey_source_read(const char *path, struct tracey_source *out);

/*
 * tracey_source_free - release memory owned by a source snapshot.
 *
 * src may be NULL. After the call, *src is reset to an empty state.
 */
void tracey_source_free(struct tracey_source *src);

/*
 * tracey_source_error_string - return a human-readable error message.
 *
 * The returned pointer refers to static storage and must not be freed.
 */
const char *tracey_source_error_string(enum tracey_source_error err);

/*
 * tracey_source_print_debug - print source debug information.
 *
 * src and out must be non-NULL. Output is written to out.
 */
void tracey_source_print_debug(const struct tracey_source *src, FILE *out);

#ifdef __cplusplus
}
#endif

#endif /* TRACEY_SOURCE_H */
