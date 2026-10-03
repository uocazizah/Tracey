#define _POSIX_C_SOURCE 200809L

#include <tracey/source.h>
#include "source_internal.h"
#include "config.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <limits.h>
#include <stdint.h>

tracey_source_t* tracey_source_read(const char* path)
{
    FILE* fp = NULL;
    struct stat st;
    size_t size = 0;
    char* content = NULL;
    size_t bytes_read = 0;
    tracey_source_t* src = NULL;
    int fd = -1;

    if (!path || path[0] == '\0') return NULL;

    /* Open with O_NOFOLLOW to avoid symlink attacks (CWE-362), then fdopen.
     * O_NOFOLLOW is mandated by POSIX.1-2001 and this file builds with
     * _POSIX_C_SOURCE 200809L, so there is deliberately NO fallback that drops
     * it: on EINVAL/EOPNOTSUPP/ENOTSUP we fail closed instead of silently
     * reopening the path without protection against symlink redirection.
     * (ELOOP still means "final component is a symlink" and is also fatal.)
     * The O_RDONLY|O_NOFOLLOW|O_CLOEXEC flags plus the fstat() and S_ISREG()
     * checks below are the accepted mitigation for this open. */
    fd = open(path, O_RDONLY | O_NOFOLLOW | O_CLOEXEC); /* flawfinder: ignore */
    if (fd < 0) return NULL;

    /* Use fstat on the fd to avoid TOCTOU race */
    if (fstat(fd, &st) != 0) {
        close(fd);
        return NULL;
    }

    /* Ensure it's a regular file (not a device, pipe, etc.) */
    if (!S_ISREG(st.st_mode)) {
        close(fd);
        return NULL;
    }

    /* Check for integer overflow - st.st_size is signed, SIZE_MAX is unsigned.
     * We need to check if st.st_size is negative (error) or exceeds SIZE_MAX. */
    if (st.st_size < 0) {
        close(fd);
        return NULL;
    }
    if ((unsigned long long)st.st_size > (unsigned long long)SIZE_MAX - 1) {
        close(fd);
        return NULL;
    }

    size = (size_t)st.st_size;

    /* Enforce maximum source file size from Kconfig */
    if (size > (size_t)CONFIG_TRACEY_MAX_SOURCE_SIZE) {
        close(fd);
        return NULL;
    }

    /* Convert fd to FILE* for buffered reading */
    fp = fdopen(fd, "rb");
    if (!fp) {
        close(fd);
        return NULL;
    }
    fd = -1; /* fp now owns the fd */

    content = malloc(size + 1);
    if (!content) {
        fclose(fp);
        return NULL;
    }

    /* Read the whole file in a loop: fread() may return a short count on
     * EOF or error, so a single call cannot be trusted to fill the buffer
     * (CWE-120/CWE-20). Any shortfall or stream error is a hard failure -
     * we never treat a partially read source as valid. */
    while (bytes_read < size) {
        size_t chunk = fread(content + bytes_read, 1, size - bytes_read, fp);
        if (chunk == 0) break;
        bytes_read += chunk;
    }
    if (ferror(fp)) {
        fclose(fp);
        free(content);
        return NULL;
    }
    fclose(fp);

    if (bytes_read != size) {
        free(content);
        return NULL;
    }

    content[size] = '\0';

    src = malloc(sizeof(tracey_source_t));
    if (!src) {
        free(content);
        return NULL;
    }

    src->path = strdup(path);
    if (!src->path) {
        free(content);
        free(src);
        return NULL;
    }

    src->content = content;
    src->size = size;

    return src;
}

void tracey_source_free(tracey_source_t* src)
{
    if (!src) return;
    free(src->path);
    free(src->content);
    free(src);
}
