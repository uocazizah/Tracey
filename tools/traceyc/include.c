#include "traceyc_internal.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/*
 * tracey_collect_include_paths - build the final include search list.
 *
 * args supplies user -I paths, which are borrowed from args. The remaining
 * entries are either string literals with static storage or canonicalized
 * environment directories owned by the array returned through pp_owned.
 *
 * The returned array is heap-allocated and owned by the caller. pp_owned
 * receives a matching array whose entries must be freed individually
 * (entries that are not owned hold NULL), or NULL when nothing was allocated.
 */
const char **tracey_collect_include_paths(const struct tracey_args *args, size_t *p_count,
                                          char ***pp_owned)
{
    const char *env_vars[2] = {"TRACEY_LIB_PATH", "TRACEY_INCLUDE_PATH"};
    size_t cap = args->include_path_count + 6;
    const char **paths;
    char **owned;
    size_t count = 0;
    size_t i;

    paths = (const char **)calloc(cap, sizeof(*paths));
    owned = (char **)calloc(cap, sizeof(*owned));
    if (paths == NULL || owned == NULL)
    {
        free(paths);
        free(owned);
        *p_count = 0;
        *pp_owned = NULL;
        return NULL;
    }

    for (i = 0; i < args->include_path_count; i++)
        paths[count++] = args->include_paths[i];

    paths[count++] = "lib";
    paths[count++] = "include";
#ifdef TRACEY_LIBDIR
    paths[count++] = TRACEY_LIBDIR;
#endif
#ifdef TRACEY_INCLUDEDIR
    paths[count++] = TRACEY_INCLUDEDIR;
#endif

    /*
     * Environment variables are untrusted input. Only existing directories
     * are accepted, and their canonical form is used so that later include
     * resolution is not affected by relative components. Invalid values are
     * ignored with a warning instead of aborting the compilation.
     */
    for (i = 0; i < sizeof(env_vars) / sizeof(env_vars[0]); i++)
    {
        const char *value = getenv(env_vars[i]); /* flawfinder: ignore: validated before canonicalization with realpath */
        char *canonical;

        if (value == NULL || value[0] == '\0')
            continue;
        if (!tracey_is_directory(value))
        {
            fprintf(stderr,
                    "Warning: ignoring environment variable %s='%s' because it is not a directory\n",
                    env_vars[i], value);
            fprintf(stderr,
                    "\033[1;34mnote:\033[0m set it to an existing directory or unset it\n");
            continue;
        }
        canonical = realpath(value, NULL); /* flawfinder: ignore: realpath used for canonicalization best practice */
        if (canonical == NULL)
        {
            fprintf(stderr, "Warning: ignoring environment variable %s='%s': %s\n", env_vars[i],
                    value, strerror(errno));
            fprintf(stderr,
                    "\033[1;34mnote:\033[0m check directory permissions and canonical path\n");
            continue;
        }
        owned[count] = canonical;
        paths[count++] = canonical;
    }

    *p_count = count;
    *pp_owned = owned;
    return paths;
}