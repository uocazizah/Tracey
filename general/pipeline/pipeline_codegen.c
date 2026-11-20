#include "tracey/pipeline.h"

#include "pipeline_internal.h"

bool tracey_pipeline_codegen(struct tracey_pipeline_file_ctx *ctx)
{
    if (ctx == NULL)
        return false;

    if (ctx->verbose)
        fprintf(stderr, "[codegen] %s\n", ctx->display_path);
    ctx->stage_start = tracey_pipeline_now_ms();

    ctx->assembly = NULL;
    ctx->assembly_len = 0;
    ctx->mem = open_memstream(&ctx->assembly, &ctx->assembly_len);
    if (ctx->mem == NULL)
    {
        fprintf(stderr, "Error: cannot allocate assembly buffer\n");
        ctx->result->had_error = true;
    }
    else
    {
        tracey_codegen_generate(&ctx->pipeline->codegen, &ctx->program, ctx->display_path, ctx->mem,
                                (int)ctx->module_id, ctx->emit_runtime_helpers);
        if (fclose(ctx->mem) != 0)
        {
            free(ctx->assembly);
            ctx->assembly = NULL;
            fprintf(stderr, "Error: cannot finalize assembly buffer\n");
            ctx->result->had_error = true;
        }
        else
        {
            ctx->result->assembly = ctx->assembly;
        }
    }

    if (ctx->verbose && !ctx->result->had_error)
        fprintf(stderr, "[codegen] done (%.3f ms)\n", tracey_pipeline_now_ms() - ctx->stage_start);

    return !ctx->result->had_error;
}
