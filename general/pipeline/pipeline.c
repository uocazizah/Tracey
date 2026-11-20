#include "tracey/pipeline.h"

#include "pipeline_internal.h"

void tracey_pipeline_init(struct tracey_compile_pipeline *pipeline,
                          const struct tracey_compile_options *opts)
{
    if (pipeline == NULL)
        return;
    memset(pipeline, 0, sizeof(*pipeline));
    if (opts != NULL)
        pipeline->opts = *opts;
    tracey_codegen_init(&pipeline->codegen, pipeline->opts.arch);
}

struct tracey_file_result tracey_pipeline_process_file(struct tracey_compile_pipeline *pipeline,
                                                       const struct tracey_source *src,
                                                       const char *path, size_t module_id,
                                                       bool emit_runtime_helpers)
{
    struct tracey_file_result result;
    struct tracey_pipeline_file_ctx ctx;

    result.assembly = NULL;
    result.had_error = false;

    if (pipeline == NULL || src == NULL || src->content == NULL)
    {
        result.had_error = true;
        return result;
    }

    memset(&ctx, 0, sizeof(ctx));
    ctx.pipeline = pipeline;
    ctx.src = src;
    ctx.path = path;
    ctx.module_id = module_id;
    ctx.emit_runtime_helpers = emit_runtime_helpers;
    ctx.display_path = path != NULL ? path : "<input>";
    ctx.verbose = pipeline->opts.verbose;
    ctx.result = &result;

    if (!tracey_pipeline_preprocess(&ctx))
        goto cleanup;
    if (!tracey_pipeline_lex(&ctx))
        goto cleanup;
    if (!tracey_pipeline_parse(&ctx))
        goto cleanup;
    if (!tracey_pipeline_semantic(&ctx))
        goto cleanup;
    if (!tracey_pipeline_codegen(&ctx))
        goto cleanup;

cleanup:
    tracey_semantic_free(&ctx.semantic);
    tracey_parser_free(&ctx.parser);
    tracey_program_free(&ctx.program);
    tracey_preprocess_result_free(&ctx.pre);
    free(ctx.base_dir);
    if (ctx.assembly != NULL && ctx.assembly != result.assembly)
        free(ctx.assembly);
    return result;
}
