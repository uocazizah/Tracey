#include "traceyc_internal.h"

#include "tracey/common.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>

/*
 * tracey_emit_assembly_output - handle the -S assembly-only output path.
 *
 * Writes generated assembly to args->output when it is non-NULL, otherwise to
 * standard output. generated is freed before returning.
 *
 * Returns 0 on success and 1 on failure.
 */
int tracey_emit_assembly_output(const struct tracey_args *args, struct tracey_string *generated)
{
    FILE *out = stdout;

    if (args->output != NULL)
    {
        out = fopen(args->output, "w");
        if (out == NULL)
        {
            fprintf(stderr, "Error: cannot write output file\n");
            fprintf(stderr, "\033[1;34mnote:\033[0m check output path and permissions\n");
            tracey_string_free(generated);
            return 1;
        }
    }
    if (generated->data != NULL)
        fwrite(generated->data, 1, generated->len, out);
    if (out != stdout && fclose(out) != 0)
    {
        fprintf(stderr, "Error: cannot finalize output file\n");
        tracey_string_free(generated);
        return 1;
    }
    tracey_string_free(generated);
    return 0;
}