#include "tracey/assembler.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*
 * tracey_read_file - read a whole file into a NUL-terminated buffer.
 *
 * Returns a heap-allocated buffer owned by the caller, or NULL on failure.
 * The buffer is always NUL-terminated; *p_len receives the byte count.
 */
static char *tracey_read_file(const char *path, size_t *p_len)
{
    FILE *f;
    char *data;
    size_t cap = 65536;
    size_t len = 0;

    f = fopen(path, "rb");
    if (f == NULL)
        return NULL;

    data = (char *)malloc(cap);
    if (data == NULL)
    {
        fclose(f);
        return NULL;
    }

    for (;;)
    {
        size_t got;

        if (len + 1 >= cap)
        {
            char *grown;

            cap *= 2;
            grown = (char *)realloc(data, cap);
            if (grown == NULL)
            {
                free(data);
                fclose(f);
                return NULL;
            }
            data = grown;
        }
        got = fread(data + len, 1, cap - len - 1, f);
        len += got;
        if (got == 0)
            break;
    }

    if (ferror(f))
    {
        free(data);
        fclose(f);
        return NULL;
    }
    fclose(f);
    data[len] = '\0';
    *p_len = len;
    return data;
}

/*
 * is_safe_output_path - validate a user-supplied output path.
 *
 * Rejects empty paths and paths containing a ".." component, so that the
 * assembler cannot be tricked into overwriting files outside the intended
 * directory through a crafted argument. A C string cannot contain an
 * embedded NUL past its terminator, so that check is implicit.
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
    char *source;
    size_t source_len = 0;
    struct tracey_asm_result result;

    if (argc < 3)
    {
        fprintf(stderr, "Error: usage: tracey-as <input.s> <output.o>\n");
        fprintf(stderr,
                "\033[1;34mnote:\033[0m provide an assembly input and output object path\n");
        return 1;
    }

    source = tracey_read_file(argv[1], &source_len);
    if (source == NULL)
    {
        fprintf(stderr, "Error: cannot read assembly input '%s'\n", argv[1]);
        fprintf(stderr, "\033[1;34mnote:\033[0m check the path and permissions\n");
        return 1;
    }

    result = tracey_assemble_aarch64(source, source_len);
    free(source);

    if (!result.ok)
    {
        fprintf(stderr, "Error: assembly failed: %s\n", result.error);
        fprintf(stderr, "\033[1;34mnote:\033[0m inspect the reported instruction and operands\n");
        tracey_asm_result_free(&result);
        return 1;
    }

    if (!is_safe_output_path(argv[2]))
    {
        fprintf(stderr, "Error: unsafe output path\n");
        fprintf(stderr,
                "\033[1;34mnote:\033[0m output path must not be empty or contain '..' "
                "components\n");
        tracey_asm_result_free(&result);
        return 1;
    }

    if (!tracey_write_elf_object(argv[2], &result))
    {
        fprintf(stderr, "Error: failed to write ELF object file\n");
        fprintf(stderr, "\033[1;34mnote:\033[0m check the output path and permissions\n");
        tracey_asm_result_free(&result);
        return 1;
    }

    tracey_asm_result_free(&result);
    return 0;
}
