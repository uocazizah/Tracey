#include <tracey/runtime.hxx>

#include <tracey/codegen.hxx>

#include <iosfwd>
#include <ostream>
#include <string>

namespace tracey
{

void emit_runtime_helpers(std::ostream &out, target_arch arch)
{
    if (arch != target_arch::aarch64)
        return;

    // Runtime helpers
    out << "    .globl __tracey_print_int\n";
    out << "__tracey_print_int:\n";
    out << "    sub sp, sp, #32\n";
    out << "    mov x1, #0\n";
    out << "    cmp x0, #0\n";
    out << "    b.ge Lpi_pos\n";
    out << "    neg x0, x0\n";
    out << "    mov x1, #1\n";
    out << "Lpi_pos:\n";
    out << "    add x2, sp, #31\n";
    out << "    mov w3, #0\n";
    out << "    strb w3, [x2]\n";
    out << "    mov x4, #0\n";
    out << "Lpi_loop:\n";
    out << "    mov x5, #10\n";
    out << "    udiv x6, x0, x5\n";
    out << "    msub x7, x6, x5, x0\n";
    out << "    add w7, w7, #48\n";
    out << "    sub x2, x2, #1\n";
    out << "    strb w7, [x2]\n";
    out << "    add x4, x4, #1\n";
    out << "    mov x0, x6\n";
    out << "    cbnz x0, Lpi_loop\n";
    out << "    cbz x1, Lpi_nosign\n";
    out << "    sub x2, x2, #1\n";
    out << "    mov w3, #45\n";
    out << "    strb w3, [x2]\n";
    out << "    add x4, x4, #1\n";
    out << "Lpi_nosign:\n";
    out << "    mov x8, #64\n";
    out << "    mov x0, #1\n";
    out << "    mov x1, x2\n";
    out << "    mov x2, x4\n";
    out << "    svc #0\n";
    out << "    add sp, sp, #32\n";
    out << "    ret\n";

    out << "    .globl __tracey_print_str\n";
    out << "__tracey_print_str:\n";
    out << "    mov x1, x0\n";
    out << "    mov x2, x0\n";
    out << "Lps_len:\n";
    out << "    ldrb w3, [x1]\n";
    out << "    cbz w3, Lps_done\n";
    out << "    add x1, x1, #1\n";
    out << "    b Lps_len\n";
    out << "Lps_done:\n";
    out << "    sub x2, x1, x2\n";
    out << "    mov x1, x0\n";
    out << "    mov x8, #64\n";
    out << "    mov x0, #1\n";
    out << "    svc #0\n";
    out << "    ret\n";

    out << "    .globl __tracey_input_int\n";
    out << "__tracey_input_int:\n";
    out << "    stp x29, x30, [sp, #-16]!\n";
    out << "    stp x19, x20, [sp, #-16]!\n";
    out << "    stp x21, x22, [sp, #-16]!\n";
    out << "    mov x19, #0\n";          // accumulated value
    out << "    mov x20, #0\n";          // sign flag
    out << "    mov x21, #0\n";          // seen digit flag
    // Skip leading whitespace.
    out << "Li_int_skipws:\n";
    out << "    bl __tracey_read_byte\n";
    out << "    cbz x1, Li_int_eof\n";
    out << "    cmp x0, #32\n";
    out << "    beq Li_int_skipws\n";
    out << "    cmp x0, #9\n";
    out << "    beq Li_int_skipws\n";
    out << "    cmp x0, #10\n";
    out << "    beq Li_int_skipws\n";
    out << "    cmp x0, #13\n";
    out << "    beq Li_int_skipws\n";
    // Optional minus sign.
    out << "    cmp x0, #45\n";
    out << "    beq Li_int_minus\n";
    // First digit?
    out << "    sub x22, x0, #48\n";
    out << "    cmp x22, #9\n";
    out << "    cset x23, ls\n";
    out << "    cbz x23, Li_int_invalid\n";
    out << "    mov x19, x22\n";
    out << "    mov x21, #1\n";
    out << "    b Li_int_loop\n";
    out << "Li_int_minus:\n";
    out << "    mov x20, #1\n";
    out << "    mov x21, #1\n";
    out << "Li_int_loop:\n";
    out << "    bl __tracey_read_byte\n";
    out << "    cbz x1, Li_int_done\n";
    out << "    sub x22, x0, #48\n";
    out << "    cmp x22, #9\n";
    out << "    cset x23, ls\n";
    out << "    cbz x23, Li_int_done\n";
    out << "    mov x2, #10\n";
    out << "    mul x3, x19, x2\n";
    out << "    add x19, x3, x22\n";
    out << "    mov x21, #1\n";
    out << "    b Li_int_loop\n";
    out << "Li_int_invalid:\n";
    out << "    mov x19, #0\n";
    out << "    mov x20, #0\n";
    out << "    mov x21, #0\n";
    out << "Li_int_done:\n";
    out << "    cbz x21, Li_int_zero\n";
    out << "    cbz x20, Li_int_ret\n";
    out << "    neg x19, x19\n";
    out << "Li_int_ret:\n";
    out << "    mov x0, x19\n";
    out << "    ldp x21, x22, [sp], #16\n";
    out << "    ldp x19, x20, [sp], #16\n";
    out << "    ldp x29, x30, [sp], #16\n";
    out << "    ret\n";
    out << "Li_int_zero:\n";
    out << "    mov x0, #0\n";
    out << "    ldp x21, x22, [sp], #16\n";
    out << "    ldp x19, x20, [sp], #16\n";
    out << "    ldp x29, x30, [sp], #16\n";
    out << "    ret\n";
    out << "Li_int_eof:\n";
    out << "    mov x0, #0\n";
    out << "    ldp x21, x22, [sp], #16\n";
    out << "    ldp x19, x20, [sp], #16\n";
    out << "    ldp x29, x30, [sp], #16\n";
    out << "    ret\n";

    out << "    .globl __tracey_input_str\n";
    out << "__tracey_input_str:\n";
    out << "    stp x29, x30, [sp, #-16]!\n";
    out << "    cmp x0, #0\n";
    out << "    beq Li_str_invalid\n";
    out << "    cmp x1, #1\n";
    out << "    cset x2, lt\n";
    out << "    cbnz x2, Li_str_invalid\n";
    out << "    stp x19, x20, [sp, #-16]!\n";
    out << "    stp x21, x22, [sp, #-16]!\n";
    out << "    mov x19, x0\n";       // buf base
    out << "    mov x20, x1\n";       // capacity
    out << "    mov x21, #0\n";       // count
    out << "Li_str_loop:\n";
    out << "    sub x22, x20, #1\n";
    out << "    cmp x21, x22\n";
    out << "    cset x2, ge\n";
    out << "    cbnz x2, Li_str_term\n";
    out << "    bl __tracey_read_byte\n";
    out << "    cbz x1, Li_str_term\n";
    out << "    cmp x0, #10\n";
    out << "    beq Li_str_term\n";
    out << "    add x2, x19, x21\n";
    out << "    strb w0, [x2]\n";
    out << "    add x21, x21, #1\n";
    out << "    b Li_str_loop\n";
    out << "Li_str_term:\n";
    out << "    mov w3, #0\n";
    out << "    add x2, x19, x21\n";
    out << "    strb w3, [x2]\n";
    out << "    mov x0, x21\n";
    out << "    ldp x21, x22, [sp], #16\n";
    out << "    ldp x19, x20, [sp], #16\n";
    out << "    ldp x29, x30, [sp], #16\n";
    out << "    ret\n";
    out << "Li_str_invalid:\n";
    out << "    ldp x29, x30, [sp], #16\n";
    out << "    mov x0, #0\n";
    out << "    ret\n";

    // Shared helper: read one byte from stdin into x0; -1 on EOF.
    out << "    .globl __tracey_read_byte\n";
    out << "__tracey_read_byte:\n";
    out << "    sub sp, sp, #16\n";
    out << "    mov x8, #63\n";
    out << "    mov x0, #0\n";
    out << "    mov x1, sp\n";
    out << "    mov x2, #1\n";
    out << "    svc #0\n";
    out << "    cmp x0, #1\n";
    out << "    beq Li_rb_ok\n";
    out << "Li_rb_eof:\n";
    out << "    mov x1, #0\n";
    out << "    mov x0, #0\n";
    out << "    add sp, sp, #16\n";
    out << "    ret\n";
    out << "Li_rb_ok:\n";
    out << "    ldrb w0, [sp]\n";
    out << "    mov x1, #1\n";
    out << "    add sp, sp, #16\n";
    out << "    ret\n";
}

} // namespace tracey