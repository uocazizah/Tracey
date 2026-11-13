#ifndef TRACEY_PIPELINE_H
#define TRACEY_PIPELINE_H

#include <stdbool.h>
#include <stddef.h>

#include "tracey/codegen.h"
#include "tracey/source.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Options controlling one compiler pipeline instance.
 *
 * include_paths is a borrowed array of NUL-terminated directories and is
 * not copied; it must outlive the pipeline.
 */
struct tracey_compile_options
{
    enum tracey_target_arch arch;
    bool verbose;
    const char *const *include_paths;
    size_t include_path_count;
};

/*
 * Result of compiling a single translation unit.
 *
 * assembly points to a heap-allocated NUL-terminated assembly text on
 * success and is NULL otherwise. The caller owns it and releases it with
 * free().
 */
struct tracey_file_result
{
    char *assembly;
    bool had_error;
};

struct tracey_compile_pipeline
{
    struct tracey_compile_options opts;
    struct tracey_code_generator codegen;
};

void tracey_pipeline_init(struct tracey_compile_pipeline *pipeline,
                          const struct tracey_compile_options *opts);

/*
 * tracey_pipeline_process_file - run preprocess, lex, parse, semantic and
 * code generation for one already-loaded source file.
 *
 * src holds the raw file contents. path is used for include resolution and
 * diagnostics. module_id namespaces generated labels so that several
 * translation units can be assembled and linked together.
 * emit_runtime_helpers requests emission of the syscall-based runtime
 * helpers; exactly one translation unit in a program should request them.
 *
 * Diagnostic messages are written to stderr. On failure had_error is set
 * and assembly is NULL.
 */
struct tracey_file_result tracey_pipeline_process_file(struct tracey_compile_pipeline *pipeline,
                                                       const struct tracey_source *src,
                                                       const char *path, size_t module_id,
                                                       bool emit_runtime_helpers);

#ifdef __cplusplus
}
#endif

#endif /* TRACEY_PIPELINE_H */
