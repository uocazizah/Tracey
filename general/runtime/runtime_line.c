#include "tracey/runtime.h"
#include "runtime_internal.h"

#include <stdio.h>

void tracey_emit_runtime_line(FILE *out)
{
    fprintf(out, "    .globl __tracey_puts\n");
    fprintf(out, "__tracey_puts:\n");
    fprintf(out, "    mov x1, x0\n");
    fprintf(out, "    mov x2, x0\n");
    fprintf(out, "Lputs_len:\n");
    fprintf(out, "    ldrb w3, [x1]\n");
    fprintf(out, "    cbz w3, Lputs_done\n");
    fprintf(out, "    add x1, x1, #1\n");
    fprintf(out, "    b Lputs_len\n");
    fprintf(out, "Lputs_done:\n");
    fprintf(out, "    sub x2, x1, x2\n");
    fprintf(out, "    mov x1, x0\n");
    fprintf(out, "    mov x8, #64\n");
    fprintf(out, "    mov x0, #1\n");
    fprintf(out, "    svc #0\n");
    /* print newline */
    fprintf(out, "    sub sp, sp, #16\n");
    fprintf(out, "    mov w3, #10\n");
    fprintf(out, "    strb w3, [sp]\n");
    fprintf(out, "    mov x8, #64\n");
    fprintf(out, "    mov x0, #1\n");
    fprintf(out, "    mov x1, sp\n");
    fprintf(out, "    mov x2, #1\n");
    fprintf(out, "    svc #0\n");
    fprintf(out, "    add sp, sp, #16\n");
    fprintf(out, "    mov x0, #0\n");
    fprintf(out, "    ret\n");

    fprintf(out, "    .globl __tracey_fget\n");
    fprintf(out, "__tracey_fget:\n");
    fprintf(out, "    stp x29, x30, [sp, #-16]!\n");
    fprintf(out, "    cbz x0, Lfget_invalid\n");
    fprintf(out, "    mov x19, x0\n");
    fprintf(out, "    mov x20, #0\n");
    fprintf(out, "Lfget_loop:\n");
    fprintf(out, "    bl __tracey_read_byte\n");
    fprintf(out, "    cbz x1, Lfget_term\n");
    fprintf(out, "    cmp x0, #10\n");
    fprintf(out, "    beq Lfget_term\n");
    fprintf(out, "    strb w0, [x19, x20]\n");
    fprintf(out, "    add x20, x20, #1\n");
    fprintf(out, "    b Lfget_loop\n");
    fprintf(out, "Lfget_term:\n");
    fprintf(out, "    strb wzr, [x19, x20]\n");
    fprintf(out, "    mov x0, x19\n");
    fprintf(out, "    ldp x29, x30, [sp], #16\n");
    fprintf(out, "    ret\n");
    fprintf(out, "Lfget_invalid:\n");
    fprintf(out, "    ldp x29, x30, [sp], #16\n");
    fprintf(out, "    mov x0, #0\n");
    fprintf(out, "    ret\n");
}

