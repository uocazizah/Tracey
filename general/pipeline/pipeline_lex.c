#include "tracey/pipeline.h"

#include "pipeline_internal.h"

bool tracey_pipeline_lex(struct tracey_pipeline_file_ctx *ctx)
{
    size_t lex_errors;
    size_t max_lexer_errors;

    if (ctx == NULL)
        return false;

    if (ctx->verbose)
        fprintf(stderr, "[lex] %s\n", ctx->display_path);
    ctx->stage_start = tracey_pipeline_now_ms();

    lex_errors = 0;
    max_lexer_errors = TRACEY_MAX_LEXER_ERRORS;

    tracey_lexer_init(&ctx->lexer, ctx->pre.content, strlen(ctx->pre.content));
    for (;;)
    {
        struct tracey_token tok = tracey_lexer_next(&ctx->lexer);
        if (tracey_token_is_error(&tok))
        {
            tracey_token_print_error(&tok, ctx->display_path, stderr);
            ctx->result->had_error = true;
            lex_errors++;
            if (lex_errors >= max_lexer_errors)
            {
                fprintf(stderr, "Error: %s: too many lexer errors, stopping\n", ctx->display_path);
                fprintf(stderr,
                        "\033[1;34mnote:\033[0m fix the reported lexer errors and recompile\n");
                break;
            }
            continue;
        }
        if (tok.type == TRACEY_TOKEN_EOF)
            break;
    }

    if (lex_errors > 0)
    {
        if (ctx->verbose)
            fprintf(stderr, "[lex] done (%.3f ms)\n", tracey_pipeline_now_ms() - ctx->stage_start);
        return false;
    }
    if (ctx->verbose)
        fprintf(stderr, "[lex] done (%.3f ms)\n", tracey_pipeline_now_ms() - ctx->stage_start);

    return true;
}
