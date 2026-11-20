#include "traceyc_internal.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

/*
 * tracey_now_ms - read a monotonic timestamp in milliseconds.
 *
 * Used for the verbose assembler/linker stage timings only.
 */
double tracey_now_ms(void)
{
    struct timespec ts;

    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0)
        return 0.0;
    return (double)ts.tv_sec * 1000.0 + (double)ts.tv_nsec / 1000000.0;
}

/*
 * tracey_make_temp_dir - create a private temporary directory.
 *
 * Returns a heap-allocated NUL-terminated path owned by the caller, or NULL
 * on failure. The directory is created with mode 0700 under $TMPDIR, or
 * /tmp when TMPDIR is unset or empty, and the caller is responsible for
 * removing the files it creates inside it.
 */
char *tracey_make_temp_dir(void)
{
    const char *base = getenv("TMPDIR"); /* flawfinder: ignore: TMPDIR used only as mkdtemp base with 0700 perms */
    size_t len;
    char *tmpl;

    if (base == NULL || base[0] == '\0')
        base = "/tmp";

    len = strlen(base) + sizeof("/traceyc.XXXXXX");
    tmpl = (char *)malloc(len);
    if (tmpl == NULL)
        return NULL;

    snprintf(tmpl, len, "%s/traceyc.XXXXXX", base);
    if (mkdtemp(tmpl) == NULL)
    {
        free(tmpl);
        return NULL;
    }
    return tmpl;
}

/*
 * tracey_is_directory - check whether path names an existing directory.
 */
bool tracey_is_directory(const char *path)
{
    struct stat st;

    if (stat(path, &st) != 0)
        return false;
    return S_ISDIR(st.st_mode) ? true : false;
}