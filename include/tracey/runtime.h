#ifndef TRACEY_RUNTIME_H
#define TRACEY_RUNTIME_H

#include <stdio.h>

#include "tracey/codegen.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * tracey_emit_runtime_helpers - emit AArch64 runtime helper routines.
 *
 * out receives the assembly text. arch selects the target architecture;
 * helpers are currently emitted only for AArch64.
 */
void tracey_emit_runtime_helpers(FILE *out, enum tracey_target_arch arch);

#ifdef __cplusplus
}
#endif

#endif /* TRACEY_RUNTIME_H */
