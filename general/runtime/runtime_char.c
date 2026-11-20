#include "tracey/runtime.h"
#include "runtime_internal.h"

#include <stdio.h>

void tracey_emit_runtime_char(FILE *out)
{
    fprintf(out, "    .globl __tracey_putchar\n");
    fprintf(out, "__tracey_putchar:\n");
    fprintf(out, "    sub sp, sp, #16\n");
    fprintf(out, "    strb w0, [sp]\n");
    fprintf(out, "    mov x8, #64\n");
    fprintf(out, "    mov x0, #1\n");
    fprintf(out, "    mov x1, sp\n");
    fprintf(out, "    mov x2, #1\n");
    fprintf(out, "    svc #0\n");
    fprintf(out, "    ldrb w0, [sp]\n");
    fprintf(out, "    add sp, sp, #16\n");
    fprintf(out, "    ret\n");

    fprintf(out, "    .globl __tracey_getchar\n");
    fprintf(out, "__tracey_getchar:\n");
    fprintf(out, "    sub sp, sp, #16\n");
    fprintf(out, "    mov x8, #63\n");
    fprintf(out, "    mov x0, #0\n");
    fprintf(out, "    mov x1, sp\n");
    fprintf(out, "    mov x2, #1\n");
    fprintf(out, "    svc #0\n");
    fprintf(out, "    cmp x0, #1\n");
    fprintf(out, "    b.ne Lgc_eof\n");
    fprintf(out, "    ldrb w0, [sp]\n");
    fprintf(out, "    add sp, sp, #16\n");
    fprintf(out, "    ret\n");
    fprintf(out, "Lgc_eof:\n");
    fprintf(out, "    mov x0, #-1\n");
    fprintf(out, "    add sp, sp, #16\n");
    fprintf(out, "    ret\n");
}

