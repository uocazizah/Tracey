#include "tracey/runtime.h"
#include "runtime_internal.h"

#include <stdio.h>

void tracey_emit_runtime_scanf(FILE *out)
{
    fprintf(out, "    .globl __tracey_scanf\n");
    fprintf(out, "__tracey_scanf:\n");
    fprintf(out, "    stp x29, x30, [sp, #-16]!\n");
    fprintf(out, "    mov x19, x1\n");
    fprintf(out, "    ldrb w2, [x0, #1]\n");
    fprintf(out, "    cmp w2, #100\n"); /* 'd' */
    fprintf(out, "    beq Lscanf_d\n");
    fprintf(out, "    mov x0, x19\n");
    fprintf(out, "    mov x1, #256\n");
    fprintf(out, "    bl __tracey_input_str\n");
    fprintf(out, "    mov x0, #1\n");
    fprintf(out, "    ldp x29, x30, [sp], #16\n");
    fprintf(out, "    ret\n");
    fprintf(out, "Lscanf_d:\n");
    fprintf(out, "    bl __tracey_input_int\n");
    fprintf(out, "    str x0, [x19]\n");
    fprintf(out, "    mov x0, #1\n");
    fprintf(out, "    ldp x29, x30, [sp], #16\n");
    fprintf(out, "    ret\n");

    fprintf(out, "    .globl __tracey_scanfs\n");
    fprintf(out, "__tracey_scanfs:\n");
    fprintf(out, "    stp x29, x30, [sp, #-16]!\n");
    fprintf(out, "    mov x19, x1\n");
    fprintf(out, "    mov x20, x2\n");
    fprintf(out, "    ldrb w3, [x0, #1]\n");
    fprintf(out, "    cmp w3, #100\n"); /* 'd' */
    fprintf(out, "    beq Lscanfs_d\n");
    fprintf(out, "    mov x0, x19\n");
    fprintf(out, "    mov x1, x20\n");
    fprintf(out, "    bl __tracey_input_str\n");
    fprintf(out, "    mov x0, #1\n");
    fprintf(out, "    ldp x29, x30, [sp], #16\n");
    fprintf(out, "    ret\n");
    fprintf(out, "Lscanfs_d:\n");
    fprintf(out, "    bl __tracey_input_int\n");
    fprintf(out, "    str x0, [x19]\n");
    fprintf(out, "    mov x0, #1\n");
    fprintf(out, "    ldp x29, x30, [sp], #16\n");
    fprintf(out, "    ret\n");
}

