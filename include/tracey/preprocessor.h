#ifndef TRACEY_PREPROCESSOR_H
#define TRACEY_PREPROCESSOR_H

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

struct tracey_preprocess_result
{
    char *content;
    bool had_error;
};

struct tracey_preprocessor_config
{
    const char *const *include_paths;
    size_t include_path_count;
    const char *base_dir;
};

/*
 * tracey_preprocess - expand #include, #define, #if, #ifdef, #ifndef,
 * #endif and #defined directives in source text.
 *
 * source points to source_len bytes of input. config controls include
 * search paths and the base directory for quoted includes. On success,
 * the returned result owns a heap-allocated NUL-terminated content
 * buffer that the caller must free with tracey_preprocess_result_free.
 *
 * The implementation supports textual inclusion of local and system
 * headers, include guards by resolved path, object-like and function-like
 * macros with full argument expansion, conditional inclusion, and reports
 * missing files to stderr while setting had_error.
 */
struct tracey_preprocess_result tracey_preprocess(const char *source, size_t source_len,
                                                   const struct tracey_preprocessor_config *config);

/*
 * tracey_preprocess_result_free - release memory owned by a preprocess result.
 */
void tracey_preprocess_result_free(struct tracey_preprocess_result *result);

#ifdef __cplusplus
}
#endif

#endif /* TRACEY_PREPROCESSOR_H */
