#ifndef TRACEY_PARSER_H
#define TRACEY_PARSER_H

#include <stdbool.h>
#include <stddef.h>

#include "tracey/ast.h"
#include "tracey/lexer.h"

#ifdef __cplusplus
extern "C" {
#endif

struct tracey_parse_error
{
    char *message;
    size_t line;
    size_t column;
};

struct tracey_parser
{
    struct tracey_lexer *lexer;
    bool had_error;
    struct tracey_parse_error *errors;
    size_t error_count;
    size_t error_cap;
    struct tracey_token current;
    bool has_current;

    /*
     * Program under construction. Set for the duration of
     * tracey_parser_parse so that struct tags can be looked up and
     * registered while parsing. May be NULL when the parser is used
     * standalone (for example in unit tests).
     */
    struct tracey_program *program;

    char **typedef_names;
    enum tracey_type_kind *typedef_types;
    struct tracey_func_sig **typedef_fn_sigs;
    size_t typedef_count;
    size_t typedef_cap;
};

/*
 * tracey_parser_init - initialize a parser over a lexer.
 *
 * lexer must remain valid for the lifetime of the parser. The parser
 * reads its first token during initialization.
 */
void tracey_parser_init(struct tracey_parser *parser, struct tracey_lexer *lexer);

/*
 * tracey_parser_free - release memory owned by parser diagnostics.
 */
void tracey_parser_free(struct tracey_parser *parser);

/*
 * tracey_parser_parse - parse a translation unit into out.
 *
 * Returns true on success with no parse errors. On failure, had_error is
 * set and errors contains diagnostics; out may contain a partial program
 * and must still be freed by the caller with tracey_program_free.
 */
bool tracey_parser_parse(struct tracey_parser *parser, struct tracey_program *out);

#ifdef __cplusplus
}
#endif

#endif /* TRACEY_PARSER_H */
