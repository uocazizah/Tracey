#include "tracey/pipeline.h"

#include "tracey/ast.h"
#include "tracey/config.h"
#include "tracey/lexer.h"
#include "tracey/parser.h"
#include "tracey/preprocessor.h"
#include "tracey/semantic.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/*
 * tracey_now_ms - read a monotonic timestamp in milliseconds.
 *
 * Used only for the verbose stage timings. CLOCK_MONOTONIC is always
 * available on the POSIX platforms this compiler supports.
 */
static double tracey_now_ms(void)
{
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0)
        return 0.0;
    return (double)ts.tv_sec * 1000.0 + (double)ts.tv_nsec / 1000000.0;
}

/*
 * tracey_dirname_dup - return the directory part of path as a new string.
 *
 * Returns "." when path contains no slash. The returned buffer is
 * heap-allocated and owned by the caller.
 */
static char *tracey_dirname_dup(const char *path)
{
    const char *slash;
    size_t len;
    char *out;

    if (path == NULL)
        return NULL;
    slash = strrchr(path, '/');
    if (slash == NULL)
        return strdup(".");
    len = (size_t)(slash - path);
    if (len == 0)
        return strdup("/");
    out = (char *)malloc(len + 1);
    if (out == NULL)
        return NULL;
    memcpy(out, path, len);
    out[len] = '\0';
    return out;
}

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
    struct tracey_preprocessor_config pre_config;
    struct tracey_preprocess_result pre;
    struct tracey_lexer lexer;
    struct tracey_parser parser;
    struct tracey_program program;
    struct tracey_semantic_analyzer semantic;
    char *base_dir;
    char *assembly;
    size_t assembly_len;
    FILE *mem;
    const char *display_path;
    bool verbose;
    double stage_start;

    result.assembly = NULL;
    result.had_error = false;

    if (pipeline == NULL || src == NULL || src->content == NULL)
    {
        result.had_error = true;
        return result;
    }

    display_path = path != NULL ? path : "<input>";
    verbose = pipeline->opts.verbose;

    /* Preprocess: textual #include expansion before any lexical analysis. */
    base_dir = tracey_dirname_dup(path != NULL ? path : "");
    pre_config.include_paths = pipeline->opts.include_paths;
    pre_config.include_path_count = pipeline->opts.include_path_count;
    pre_config.base_dir = base_dir;

    if (verbose)
        fprintf(stderr, "[preprocess] %s\n", display_path);
    stage_start = tracey_now_ms();

    pre = tracey_preprocess(src->content, src->size, &pre_config);
    if (pre.had_error)
    {
        tracey_preprocess_result_free(&pre);
        free(base_dir);
        result.had_error = true;
        return result;
    }
    if (verbose)
        fprintf(stderr, "[preprocess] done (%.3f ms)\n", tracey_now_ms() - stage_start);

    /* Lex once up front so that lexical errors are reported before parsing. */
    if (verbose)
        fprintf(stderr, "[lex] %s\n", display_path);
    stage_start = tracey_now_ms();
    {
        size_t lex_errors = 0;
        size_t max_lexer_errors = TRACEY_MAX_LEXER_ERRORS;

        tracey_lexer_init(&lexer, pre.content, strlen(pre.content));
        for (;;)
        {
            struct tracey_token tok = tracey_lexer_next(&lexer);
            if (tracey_token_is_error(&tok))
            {
                tracey_token_print_error(&tok, display_path, stderr);
                result.had_error = true;
                lex_errors++;
                if (lex_errors >= max_lexer_errors)
                {
                    fprintf(stderr, "Error: %s: too many lexer errors, stopping\n", display_path);
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
            if (verbose)
                fprintf(stderr, "[lex] done (%.3f ms)\n", tracey_now_ms() - stage_start);
            tracey_preprocess_result_free(&pre);
            free(base_dir);
            return result;
        }
    }
    if (verbose)
        fprintf(stderr, "[lex] done (%.3f ms)\n", tracey_now_ms() - stage_start);

    /* Parse into the C AST. */
    if (verbose)
        fprintf(stderr, "[parse] %s\n", display_path);
    stage_start = tracey_now_ms();

    tracey_lexer_init(&lexer, pre.content, strlen(pre.content));
    tracey_parser_init(&parser, &lexer);
    tracey_program_init(&program);

    if (!tracey_parser_parse(&parser, &program))
    {
        size_t i;
        for (i = 0; i < parser.error_count; i++)
        {
            fprintf(stderr, "%s:%zu:%zu: \033[1;31merror:\033[0m %s\n", display_path,
                    parser.errors[i].line, parser.errors[i].column,
                    parser.errors[i].message ? parser.errors[i].message : "syntax error");
            fprintf(stderr,
                    "%s:%zu:%zu: \033[1;34mnote:\033[0m review the syntax and types at this location\n",
                    display_path, parser.errors[i].line, parser.errors[i].column);
        }
        result.had_error = true;
        if (verbose)
            fprintf(stderr, "[parse] failed (%.3f ms)\n", tracey_now_ms() - stage_start);
        tracey_parser_free(&parser);
        tracey_program_free(&program);
        tracey_preprocess_result_free(&pre);
        free(base_dir);
        return result;
    }
    if (verbose)
        fprintf(stderr, "[parse] done (%.3f ms)\n", tracey_now_ms() - stage_start);

    /* Semantic analysis. */
    if (verbose)
        fprintf(stderr, "[semantic] %s\n", display_path);
    stage_start = tracey_now_ms();

    tracey_semantic_init(&semantic);
    if (!tracey_semantic_analyze(&semantic, &program))
    {
        size_t i;
        for (i = 0; i < semantic.error_count; i++)
        {
            fprintf(stderr, "%s:%zu:%zu: \033[1;31merror:\033[0m %s\n", display_path,
                    semantic.errors[i].line, semantic.errors[i].column,
                    semantic.errors[i].message ? semantic.errors[i].message : "semantic error");
            fprintf(stderr,
                    "%s:%zu:%zu: \033[1;34mnote:\033[0m review the syntax and types at this location\n",
                    display_path, semantic.errors[i].line, semantic.errors[i].column);
        }
        result.had_error = true;
        if (verbose)
            fprintf(stderr, "[semantic] failed (%.3f ms)\n", tracey_now_ms() - stage_start);
        tracey_semantic_free(&semantic);
        tracey_parser_free(&parser);
        tracey_program_free(&program);
        tracey_preprocess_result_free(&pre);
        free(base_dir);
        return result;
    }
    if (verbose)
        fprintf(stderr, "[semantic] done (%.3f ms)\n", tracey_now_ms() - stage_start);

    /* Code generation. open_memstream gives the generator a plain FILE *
     * while still returning a NUL-terminated buffer to the caller. */
    if (verbose)
        fprintf(stderr, "[codegen] %s\n", display_path);
    stage_start = tracey_now_ms();

    assembly = NULL;
    assembly_len = 0;
    mem = open_memstream(&assembly, &assembly_len);
    if (mem == NULL)
    {
        fprintf(stderr, "Error: cannot allocate assembly buffer\n");
        result.had_error = true;
    }
    else
    {
        tracey_codegen_generate(&pipeline->codegen, &program, display_path, mem, (int)module_id,
                                emit_runtime_helpers);
        if (fclose(mem) != 0)
        {
            free(assembly);
            assembly = NULL;
            fprintf(stderr, "Error: cannot finalize assembly buffer\n");
            result.had_error = true;
        }
        else
        {
            result.assembly = assembly;
        }
    }

    if (verbose && !result.had_error)
        fprintf(stderr, "[codegen] done (%.3f ms)\n", tracey_now_ms() - stage_start);

    tracey_semantic_free(&semantic);
    tracey_parser_free(&parser);
    tracey_program_free(&program);
    tracey_preprocess_result_free(&pre);
    free(base_dir);
    return result;
}
