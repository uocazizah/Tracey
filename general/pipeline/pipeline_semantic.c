#include "tracey/pipeline.h"

#include "pipeline_internal.h"

bool tracey_pipeline_semantic(struct tracey_pipeline_file_ctx *ctx)
{
    size_t i;
    size_t wi;

    if (ctx == NULL)
        return false;

    if (ctx->verbose)
        fprintf(stderr, "[semantic] %s\n", ctx->display_path);
    ctx->stage_start = tracey_pipeline_now_ms();

    tracey_semantic_init(&ctx->semantic);
    if (!tracey_semantic_analyze(&ctx->semantic, &ctx->program))
    {
        for (i = 0; i < ctx->semantic.error_count; i++)
        {
            fprintf(stderr, "%s:%zu:%zu: \033[1;31merror:\033[0m %s\n", ctx->display_path,
                    ctx->semantic.errors[i].line, ctx->semantic.errors[i].column,
                    ctx->semantic.errors[i].message ? ctx->semantic.errors[i].message : "semantic error");
            fprintf(stderr,
                    "%s:%zu:%zu: \033[1;34mnote:\033[0m review the syntax and types at this location\n",
                    ctx->display_path, ctx->semantic.errors[i].line, ctx->semantic.errors[i].column);
        }
        ctx->result->had_error = true;
        if (ctx->verbose)
            fprintf(stderr, "[semantic] failed (%.3f ms)\n", tracey_pipeline_now_ms() - ctx->stage_start);
        return false;
    }
    if (ctx->semantic.warning_count > 0)
    {
        for (wi = 0; wi < ctx->semantic.warning_count; wi++)
        {
            fprintf(stderr, "%s:%zu:%zu: \033[1;33mwarning:\033[0m %s\n", ctx->display_path,
                    ctx->semantic.warnings[wi].line, ctx->semantic.warnings[wi].column,
                    ctx->semantic.warnings[wi].message ? ctx->semantic.warnings[wi].message : "semantic warning");
            fprintf(stderr,
                    "%s:%zu:%zu: \033[1;34mnote:\033[0m review the syntax and types at this location\n",
                    ctx->display_path, ctx->semantic.warnings[wi].line, ctx->semantic.warnings[wi].column);
        }
        if (ctx->pipeline->opts.werror)
        {
            ctx->result->had_error = true;
            return false;
        }
    }
    if (ctx->verbose)
        fprintf(stderr, "[semantic] done (%.3f ms)\n", tracey_pipeline_now_ms() - ctx->stage_start);

    return true;
}
