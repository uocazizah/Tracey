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
#include <errno.h>
#include <stdint.h>

tracey_source_t* tracey_source_read(const char* path)
{
    FILE* fp = NULL;
    struct stat st;
    size_t size = 0;
    char* content = NULL;
    size_t read = 0;
    tracey_source_t* src = NULL;
    int fd = -1;

    if (!path || path[0] == '\0') return NULL;

    /* Open with O_NOFOLLOW to avoid symlink attacks, then fdopen */
    fd = open(path, O_RDONLY | O_NOFOLLOW | O_CLOEXEC);
    if (fd < 0) {
        int err = errno;
        /* Fallback for systems without O_NOFOLLOW support (EINVAL, EOPNOTSUPP, ENOTSUP).
         * Do NOT fallback on ELOOP - when O_NOFOLLOW is specified, ELOOP means
         * the final component is a symlink and we explicitly requested not to follow it. */
        if (err == EINVAL || err == EOPNOTSUPP || err == ENOTSUP) {
            fd = open(path, O_RDONLY | O_CLOEXEC);
        }
        if (fd < 0) return NULL;
    }

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

    read = fread(content, 1, size, fp);
    fclose(fp);

    if (read != size) {
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
