#ifndef TRACEY_RUNTIME_INTERNAL_H
#define TRACEY_RUNTIME_INTERNAL_H

#include "tracey/runtime.h"

#include <stdio.h>

/*
 * Per-group emitters for the AArch64 runtime helper routines. Each function
 * writes one logical group of helpers to out; tracey_emit_runtime_helpers
 * calls them in order so the combined output matches the original single-file
 * emission.
 */
void tracey_emit_runtime_print(FILE *out);
void tracey_emit_runtime_input(FILE *out);
void tracey_emit_runtime_char(FILE *out);
void tracey_emit_runtime_line(FILE *out);
void tracey_emit_runtime_scanf(FILE *out);
void tracey_emit_runtime_read(FILE *out);

#endif /* TRACEY_RUNTIME_INTERNAL_H */