#include "tracey/linker.h"

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*
 * is_safe_output_path - validate a user-supplied output path.
 *
 * Rejects empty paths and paths containing a ".." component, so that the
 * linker cannot be tricked into overwriting files outside the intended
 * directory through a crafted argument.
 */
static bool is_safe_output_path(const char *p)
{
    const char *part;

    if (p == NULL || p[0] == '\0')
        return false;

    part = p;
    for (;;)
    {
        const char *slash = strchr(part, '/');
        size_t len = slash != NULL ? (size_t)(slash - part) : strlen(part);

        if (len == 2 && part[0] == '.' && part[1] == '.')
            return false;
        if (slash == NULL)
            break;
        part = slash + 1;
    }
    return true;
}

int main(int argc, char **argv)
{
    const char **objects;
    int object_count;
    int i;
    bool ok;

    if (argc < 3)
    {
        fprintf(stderr, "Error: usage: tracey-ld <out.elf> <obj1.o> [obj2.o...]\n");
        fprintf(stderr,
                "\033[1;34mnote:\033[0m provide an output executable and at least one object "
                "file\n");
        return 1;
    }

    object_count = argc - 2;
    objects = (const char **)malloc((size_t)object_count * sizeof(*objects));
    if (objects == NULL)
    {
        fprintf(stderr, "Error: out of memory while collecting object files\n");
        return 1;
    }
    for (i = 0; i < object_count; i++)
        objects[i] = argv[i + 2];

    if (!is_safe_output_path(argv[1]))
    {
        fprintf(stderr, "Error: unsafe output path\n");
        fprintf(stderr,
                "\033[1;34mnote:\033[0m output path must not be empty or contain '..' "
                "components\n");
        free(objects);
        return 1;
    }

    ok = tracey_link_elf_aarch64(objects, (size_t)object_count, argv[1]);
    if (!ok)
    {
        fprintf(stderr, "Error: linking failed\n");
        fprintf(stderr,
                "\033[1;34mnote:\033[0m verify object files, entry point, and unresolved "
                "symbols\n");
    }

    free(objects);
    return ok ? 0 : 1;
}
