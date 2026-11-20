#include "tracey/pipeline.h"

#include "pipeline_internal.h"

bool tracey_pipeline_parse(struct tracey_pipeline_file_ctx *ctx)
{
    size_t i;

    if (ctx == NULL)
        return false;

    if (ctx->verbose)
        fprintf(stderr, "[parse] %s\n", ctx->display_path);
    ctx->stage_start = tracey_pipeline_now_ms();

    tracey_lexer_init(&ctx->lexer, ctx->pre.content, strlen(ctx->pre.content));
    tracey_parser_init(&ctx->parser, &ctx->lexer);
    tracey_program_init(&ctx->program);

    if (!tracey_parser_parse(&ctx->parser, &ctx->program))
    {
        for (i = 0; i < ctx->parser.error_count; i++)
        {
            fprintf(stderr, "%s:%zu:%zu: \033[1;31merror:\033[0m %s\n", ctx->display_path,
                    ctx->parser.errors[i].line, ctx->parser.errors[i].column,
                    ctx->parser.errors[i].message ? ctx->parser.errors[i].message : "syntax error");
            fprintf(stderr,
                    "%s:%zu:%zu: \033[1;34mnote:\033[0m review the syntax and types at this location\n",
                    ctx->display_path, ctx->parser.errors[i].line, ctx->parser.errors[i].column);
        }
        ctx->result->had_error = true;
        if (ctx->verbose)
            fprintf(stderr, "[parse] failed (%.3f ms)\n", tracey_pipeline_now_ms() - ctx->stage_start);
        return false;
    }
    if (ctx->verbose)
        fprintf(stderr, "[parse] done (%.3f ms)\n", tracey_pipeline_now_ms() - ctx->stage_start);

    return true;
}
