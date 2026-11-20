#ifndef TRACEY_PIPELINE_INTERNAL_H
#define TRACEY_PIPELINE_INTERNAL_H

#include "tracey/pipeline.h"

#include "tracey/ast.h"
#include "tracey/config.h"
#include "tracey/lexer.h"
#include "tracey/parser.h"
#include "tracey/preprocessor.h"
#include "tracey/semantic.h"
#include "tracey/source.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/*
 * tracey_pipeline_file_ctx - per-file state shared by the pipeline stage
 * helpers.
 *
 * The structure owns the intermediate objects created while compiling a
 * single translation unit. It is zero-initialized by the orchestrator
 * before the first stage runs; every owned pointer is either NULL or a
 * valid object, so the cleanup path can release them unconditionally.
 */
struct tracey_pipeline_file_ctx
{
    struct tracey_compile_pipeline *pipeline;
    const struct tracey_source *src;
    const char *path;
    size_t module_id;
    bool emit_runtime_helpers;

    const char *display_path;
    bool verbose;

    struct tracey_file_result *result;

    char *base_dir;
    struct tracey_preprocessor_config pre_config;
    struct tracey_preprocess_result pre;
    struct tracey_lexer lexer;
    struct tracey_parser parser;
    struct tracey_program program;
    struct tracey_semantic_analyzer semantic;
    char *assembly;
    size_t assembly_len;
    FILE *mem;
    double stage_start;
};

/*
 * Utility helpers shared by the pipeline compilation units.
 */
double tracey_pipeline_now_ms(void);
char *tracey_pipeline_dirname_dup(const char *path);

/*
 * Stage helpers. Each returns true on success and false on failure. On
 * failure the helper records the error in ctx->result->had_error and
 * prints diagnostics; resource release is left to the orchestrator.
 */
bool tracey_pipeline_preprocess(struct tracey_pipeline_file_ctx *ctx);
bool tracey_pipeline_lex(struct tracey_pipeline_file_ctx *ctx);
bool tracey_pipeline_parse(struct tracey_pipeline_file_ctx *ctx);
bool tracey_pipeline_semantic(struct tracey_pipeline_file_ctx *ctx);
bool tracey_pipeline_codegen(struct tracey_pipeline_file_ctx *ctx);

#endif /* TRACEY_PIPELINE_INTERNAL_H */
