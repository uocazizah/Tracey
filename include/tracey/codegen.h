#ifndef TRACEY_CODEGEN_H
#define TRACEY_CODEGEN_H

#include <stdbool.h>
#include <stdio.h>

#include "tracey/ast.h"

#ifdef __cplusplus
extern "C" {
#endif

enum tracey_target_arch
{
    TRACEY_TARGET_AARCH64 = 0,
    TRACEY_TARGET_X86_64,
};

struct tracey_code_generator
{
    enum tracey_target_arch arch;
};

void tracey_codegen_init(struct tracey_code_generator *cg, enum tracey_target_arch arch);

/*
 * tracey_codegen_generate - generate assembly for a parsed program.
 *
 * program is the input AST. source_name is used for debug/file metadata.
 * out receives assembly text. module_id namespaces generated labels.
 * When emit_runtime_helpers is true, runtime helper routines are appended.
 */
void tracey_codegen_generate(struct tracey_code_generator *cg, const struct tracey_program *program,
                             const char *source_name, FILE *out, int module_id,
                             bool emit_runtime_helpers);

#ifdef __cplusplus
}
#endif

#endif /* TRACEY_CODEGEN_H */
