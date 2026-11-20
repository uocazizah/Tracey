#include "tracey/pipeline.h"

#include "pipeline_internal.h"

/*
 * tracey_now_ms - read a monotonic timestamp in milliseconds.
 *
 * Used only for the verbose stage timings. CLOCK_MONOTONIC is always
 * available on the POSIX platforms this compiler supports.
 */
double tracey_pipeline_now_ms(void)
{
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0)
        return 0.0;
    return (double)ts.tv_sec * 1000.0 + (double)ts.tv_nsec / 1000000.0;
}

/*
 * tracey_dirname_dup - return the directory part of path as a new string.
 *
 * Returns "." when path contains no slash. The returned buffer is
 * heap-allocated and owned by the caller.
 */
char *tracey_pipeline_dirname_dup(const char *path)
{
    const char *slash;
    size_t len;
    char *out;

    if (path == NULL)
        return NULL;
    slash = strrchr(path, '/');
    if (slash == NULL)
        return strdup(".");
    len = (size_t)(slash - path);
    if (len == 0)
        return strdup("/");
    out = (char *)malloc(len + 1);
    if (out == NULL)
        return NULL;
    memcpy(out, path, len);
    out[len] = '\0';
    return out;
}
