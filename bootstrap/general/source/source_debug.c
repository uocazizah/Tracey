#include <tracey/source.h>
#include "source_internal.h"
#include <stdio.h>

void tracey_source_print_debug(const tracey_source_t* src, FILE* out)
{
    if (!out) out = stdout;
    if (!src) {
        fprintf(out, "Source: NULL\n");
        return;
    }

    fprintf(out, "Source:\n");
    fprintf(out, "  Path: %s\n", src->path ? src->path : "(unknown)");
    fprintf(out, "  Size: %zu bytes\n", src->size);
    fprintf(out, "  Empty: %s\n", src->size == 0 ? "yes" : "no");

    if (src->size > 0) {
        fprintf(out, "  Content:\n");
        fprintf(out, "----\n");
        fwrite(src->content, 1, src->size, out);
        if (src->content[src->size - 1] != '\n') fprintf(out, "\n");
        fprintf(out, "----\n");
    }
}