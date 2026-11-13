#include "tracey/source.h"

#include "tracey/config.h"

#include <errno.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(__unix__)
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

/*
 * tracey_source_init_empty - initialize a source snapshot to empty state.
 *
 * src must be non-NULL. No memory is allocated.
 */
static void tracey_source_init_empty(struct tracey_source *src)
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

#if defined(__unix__)

/*
 * tracey_source_read - read a source file from disk (POSIX implementation).
 *
 * The file is opened with O_NOFOLLOW to avoid following symlinks. After
 * opening, fstat is used on the file descriptor to ensure the object is a
 * regular file and to obtain a trusted size. The content is read in a
 * loop to handle short reads. On success the caller owns the allocated
 * path and content buffers in out.
 */
enum tracey_source_error tracey_source_read(const char *path, struct tracey_source *out)
{
    int fd = -1;
    FILE *fp = NULL;
    struct stat st;
    size_t size;
    size_t bytes_read = 0;
    char *content = NULL;
    char *path_copy = NULL;

    if (out == NULL)
        return TRACEY_SOURCE_INVALID_PATH;
    tracey_source_init_empty(out);

    if (path == NULL || path[0] == '\0')
        return TRACEY_SOURCE_INVALID_PATH;

    fd = open(path, O_RDONLY | O_NOFOLLOW | O_CLOEXEC);
    if (fd < 0)
        return TRACEY_SOURCE_OPEN_FAILED;

    if (fstat(fd, &st) != 0)
    {
        close(fd);
        return TRACEY_SOURCE_OPEN_FAILED;
    }

    if (!S_ISREG(st.st_mode))
    {
        close(fd);
        return TRACEY_SOURCE_NOT_REGULAR_FILE;
    }

    if (st.st_size < 0)
    {
        close(fd);
        return TRACEY_SOURCE_READ_FAILED;
    }

    if ((unsigned long long)st.st_size > (unsigned long long)SIZE_MAX - 1ULL)
    {
        close(fd);
        return TRACEY_SOURCE_READ_FAILED;
    }

    size = (size_t)st.st_size;
    if (size > TRACEY_MAX_SOURCE_SIZE)
    {
        close(fd);
        return TRACEY_SOURCE_TOO_LARGE;
    }

    fp = fdopen(fd, "rb");
    if (fp == NULL)
    {
        close(fd);
        return TRACEY_SOURCE_OPEN_FAILED;
    }

    /*
     * fdopen takes ownership of fd on success. From this point on, closing
     * fp closes the underlying descriptor.
     */
    fd = -1;

    content = (char *)malloc(size + 1);
    if (content == NULL)
    {
        fclose(fp);
        return TRACEY_SOURCE_NO_MEMORY;
    }

    while (bytes_read < size)
    {
        size_t chunk = fread(content + bytes_read, 1, size - bytes_read, fp);
        if (chunk == 0)
            break;
        bytes_read += chunk;
    }

    if (ferror(fp) || bytes_read != size)
    {
        free(content);
        fclose(fp);
        return TRACEY_SOURCE_READ_FAILED;
    }

    content[size] = '\0';
    fclose(fp);

    path_copy = strdup(path);
    if (path_copy == NULL)
    {
        free(content);
        return TRACEY_SOURCE_NO_MEMORY;
    }

    out->path = path_copy;
    out->content = content;
    out->size = size;

    TRACEY_TRACE("[source] read '%s' (%zu bytes)\n", path, size);
    return TRACEY_SOURCE_OK;
}

#else

/*
 * tracey_source_read - read a source file from disk (portable fallback).
 *
 * This path is used on non-POSIX platforms. It uses stdio operations and
 * best-effort validation available without POSIX-specific file descriptor
 * APIs.
 */
enum tracey_source_error tracey_source_read(const char *path, struct tracey_source *out)
{
    FILE *fp = NULL;
    long file_size;
    size_t size;
    size_t bytes_read = 0;
    char *content = NULL;
    char *path_copy = NULL;

    if (out == NULL)
        return TRACEY_SOURCE_INVALID_PATH;
    tracey_source_init_empty(out);

    if (path == NULL || path[0] == '\0')
        return TRACEY_SOURCE_INVALID_PATH;

    fp = fopen(path, "rb");
    if (fp == NULL)
        return TRACEY_SOURCE_OPEN_FAILED;

    if (fseek(fp, 0, SEEK_END) != 0)
    {
        fclose(fp);
        return TRACEY_SOURCE_READ_FAILED;
    }

    file_size = ftell(fp);
    if (file_size < 0)
    {
        fclose(fp);
        return TRACEY_SOURCE_READ_FAILED;
    }

    if (fseek(fp, 0, SEEK_SET) != 0)
    {
        fclose(fp);
        return TRACEY_SOURCE_READ_FAILED;
    }

    if ((unsigned long)file_size > (unsigned long)TRACEY_MAX_SOURCE_SIZE)
    {
        fclose(fp);
        return TRACEY_SOURCE_TOO_LARGE;
    }

    size = (size_t)file_size;
    content = (char *)malloc(size + 1);
    if (content == NULL)
    {
        fclose(fp);
        return TRACEY_SOURCE_NO_MEMORY;
    }

    while (bytes_read < size)
    {
        size_t chunk = fread(content + bytes_read, 1, size - bytes_read, fp);
        if (chunk == 0)
            break;
        bytes_read += chunk;
    }

    if (ferror(fp) || bytes_read != size)
    {
        free(content);
        fclose(fp);
        return TRACEY_SOURCE_READ_FAILED;
    }

    content[size] = '\0';
    fclose(fp);

    path_copy = strdup(path);
    if (path_copy == NULL)
    {
        free(content);
        return TRACEY_SOURCE_NO_MEMORY;
    }

    out->path = path_copy;
    out->content = content;
    out->size = size;

    TRACEY_TRACE("[source] read '%s' (%zu bytes, portable path)\n", path, size);
    return TRACEY_SOURCE_OK;
}

#endif
