#include "tracey/runtime.h"

#include "runtime_internal.h"

/*
 * tracey_emit_runtime_helpers - emit AArch64 runtime helper routines.
 *
 * out receives the assembly text. arch selects the target architecture;
 * helpers are currently emitted only for AArch64. The individual helper
 * groups live in separate compilation units and are invoked here in their
 * original emission order.
 */
void tracey_emit_runtime_helpers(FILE *out, enum tracey_target_arch arch)
{
    if (out == NULL || arch != TRACEY_TARGET_AARCH64)
        return;

    tracey_emit_runtime_print(out);
    tracey_emit_runtime_input(out);
    tracey_emit_runtime_char(out);
    tracey_emit_runtime_line(out);
    tracey_emit_runtime_scanf(out);
    tracey_emit_runtime_read(out);
}