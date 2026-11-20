#include "tracey/runtime.h"
#include "runtime_internal.h"

#include <stdio.h>

void tracey_emit_runtime_print(FILE *out)
{
    fprintf(out, "    .globl __tracey_print_int\n");
    fprintf(out, "__tracey_print_int:\n");
    fprintf(out, "    sub sp, sp, #32\n");
    fprintf(out, "    mov x1, #0\n");
    fprintf(out, "    cmp x0, #0\n");
    fprintf(out, "    b.ge Lpi_pos\n");
    fprintf(out, "    neg x0, x0\n");
    fprintf(out, "    mov x1, #1\n");
    fprintf(out, "Lpi_pos:\n");
    fprintf(out, "    add x2, sp, #31\n");
    fprintf(out, "    mov w3, #0\n");
    fprintf(out, "    strb w3, [x2]\n");
    fprintf(out, "    mov x4, #0\n");
    fprintf(out, "Lpi_loop:\n");
    fprintf(out, "    mov x5, #10\n");
    fprintf(out, "    udiv x6, x0, x5\n");
    fprintf(out, "    msub x7, x6, x5, x0\n");
    fprintf(out, "    add w7, w7, #48\n");
    fprintf(out, "    sub x2, x2, #1\n");
    fprintf(out, "    strb w7, [x2]\n");
    fprintf(out, "    add x4, x4, #1\n");
    fprintf(out, "    mov x0, x6\n");
    fprintf(out, "    cbnz x0, Lpi_loop\n");
    fprintf(out, "    cbz x1, Lpi_nosign\n");
    fprintf(out, "    sub x2, x2, #1\n");
    fprintf(out, "    mov w3, #45\n");
    fprintf(out, "    strb w3, [x2]\n");
    fprintf(out, "    add x4, x4, #1\n");
    fprintf(out, "Lpi_nosign:\n");
    fprintf(out, "    mov x8, #64\n");
    fprintf(out, "    mov x0, #1\n");
    fprintf(out, "    mov x1, x2\n");
    fprintf(out, "    mov x2, x4\n");
    fprintf(out, "    svc #0\n");
    fprintf(out, "    add sp, sp, #32\n");
    fprintf(out, "    ret\n");

    fprintf(out, "    .globl __tracey_print_str\n");
    fprintf(out, "__tracey_print_str:\n");
    fprintf(out, "    mov x1, x0\n");
    fprintf(out, "    mov x2, x0\n");
    fprintf(out, "Lps_len:\n");
    fprintf(out, "    ldrb w3, [x1]\n");
    fprintf(out, "    cbz w3, Lps_done\n");
    fprintf(out, "    add x1, x1, #1\n");
    fprintf(out, "    b Lps_len\n");
    fprintf(out, "Lps_done:\n");
    fprintf(out, "    sub x2, x1, x2\n");
    fprintf(out, "    mov x1, x0\n");
    fprintf(out, "    mov x8, #64\n");
    fprintf(out, "    mov x0, #1\n");
    fprintf(out, "    svc #0\n");
    fprintf(out, "    ret\n");
}

