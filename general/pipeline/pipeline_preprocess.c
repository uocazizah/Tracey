#include "tracey/pipeline.h"

#include "pipeline_internal.h"

bool tracey_pipeline_preprocess(struct tracey_pipeline_file_ctx *ctx)
{
    if (ctx == NULL)
        return false;

    ctx->base_dir = tracey_pipeline_dirname_dup(ctx->path != NULL ? ctx->path : "");
    ctx->pre_config.include_paths = ctx->pipeline->opts.include_paths;
    ctx->pre_config.include_path_count = ctx->pipeline->opts.include_path_count;
    ctx->pre_config.base_dir = ctx->base_dir;

    if (ctx->verbose)
        fprintf(stderr, "[preprocess] %s\n", ctx->display_path);
    ctx->stage_start = tracey_pipeline_now_ms();

    ctx->pre = tracey_preprocess(ctx->src->content, ctx->src->size, &ctx->pre_config);
    if (ctx->pre.had_error)
    {
        ctx->result->had_error = true;
        return false;
    }
    if (ctx->verbose)
        fprintf(stderr, "[preprocess] done (%.3f ms)\n", tracey_pipeline_now_ms() - ctx->stage_start);

    return true;
}
