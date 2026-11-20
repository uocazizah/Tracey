#include "tracey/runtime.h"
#include "runtime_internal.h"

#include <stdio.h>

void tracey_emit_runtime_read(FILE *out)
{
    fprintf(out, "    .globl __tracey_read_byte\n");
    fprintf(out, "__tracey_read_byte:\n");
    fprintf(out, "    sub sp, sp, #16\n");
    fprintf(out, "    mov x8, #63\n");
    fprintf(out, "    mov x0, #0\n");
    fprintf(out, "    mov x1, sp\n");
    fprintf(out, "    mov x2, #1\n");
    fprintf(out, "    svc #0\n");
    fprintf(out, "    cmp x0, #1\n");
    fprintf(out, "    beq Li_rb_ok\n");
    fprintf(out, "Li_rb_eof:\n");
    fprintf(out, "    mov x1, #0\n");
    fprintf(out, "    mov x0, #0\n");
    fprintf(out, "    add sp, sp, #16\n");
    fprintf(out, "    ret\n");
    fprintf(out, "Li_rb_ok:\n");
    fprintf(out, "    ldrb w0, [sp]\n");
    fprintf(out, "    mov x1, #1\n");
    fprintf(out, "    add sp, sp, #16\n");
    fprintf(out, "    ret\n");
}

