#include "traceyc_internal.h"

#include "tracey/assembler.h"
#include "tracey/common.h"
#include "tracey/linker.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

/*
 * tracey_build_executable - assemble, link, and finalize an executable.
 *
 * generated holds the concatenated assembly text. args supplies output path
 * and verbosity. generated is freed before returning.
 *
 * Returns 0 on success and 1 on failure.
 */
int tracey_build_executable(const struct tracey_args *args, struct tracey_string *generated)
{
    const char *exe_path = args->output != NULL ? args->output : "a.out";
    char *tmp_dir = tracey_make_temp_dir();
    char *obj_path = NULL;
    struct tracey_asm_result asm_result;
    bool ok = true;
    bool verbose = args->verbose;

    if (tmp_dir == NULL)
    {
        fprintf(stderr, "Error: cannot create temporary directory\n");
        fprintf(stderr, "\033[1;34mnote:\033[0m set TMPDIR to a writable directory\n");
        tracey_string_free(generated);
        return 1;
    }

    obj_path = tracey_join_path(tmp_dir, "traceyc.o");
    if (obj_path == NULL)
    {
        fprintf(stderr, "Error: out of memory while preparing temporary paths\n");
        rmdir(tmp_dir);
        free(tmp_dir);
        tracey_string_free(generated);
        return 1;
    }

    if (verbose)
        fprintf(stderr, "[assemble] start\n");
    {
        double start = tracey_now_ms();
        size_t generated_len = generated->data != NULL ? generated->len : 0;

        asm_result = tracey_assemble_aarch64(generated->data != NULL ? generated->data : "",
                                            generated_len);
        if (verbose)
            fprintf(stderr, "[assemble] done (%.3f ms)\n", tracey_now_ms() - start);
    }

    if (!asm_result.ok)
    {
        fprintf(stderr, "Error: assembler failed: %s\n", asm_result.error);
        fprintf(stderr,
                "\033[1;34mnote:\033[0m inspect generated assembly around the reported "
                "instruction\n");
        ok = false;
    }
    else if (!tracey_write_elf_object(obj_path, &asm_result))
    {
        fprintf(stderr, "Error: cannot write object file\n");
        fprintf(stderr, "\033[1;34mnote:\033[0m check temporary directory permissions\n");
        ok = false;
    }
    tracey_asm_result_free(&asm_result);

    if (ok)
    {
        const char *objects[1];

        objects[0] = obj_path;
        if (verbose)
            fprintf(stderr, "[link] start\n");
        {
            double start = tracey_now_ms();

            if (!tracey_link_elf_aarch64(objects, 1, exe_path))
            {
                fprintf(stderr, "Error: linking failed\n");
                fprintf(stderr,
                        "\033[1;34mnote:\033[0m verify object files, entry point, and "
                        "unresolved symbols\n");
                ok = false;
            }
            if (verbose)
                fprintf(stderr, "[link] done (%.3f ms)\n", tracey_now_ms() - start);
        }
    }

    if (ok)
    {
        struct stat st;
        if (stat(exe_path, &st) == 0)
            (void)chmod(exe_path, st.st_mode | S_IXUSR | S_IXGRP | S_IXOTH); /* flawfinder: ignore: chmod on newly created output file */
        printf("Output: %s\n", exe_path);
    }

    unlink(obj_path);
    rmdir(tmp_dir);
    free(obj_path);
    free(tmp_dir);

    tracey_string_free(generated);
    return ok ? 0 : 1;
}