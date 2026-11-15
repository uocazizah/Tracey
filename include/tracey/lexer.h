#ifndef TRACEY_LEXER_H
#define TRACEY_LEXER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Token types produced by the lexer. */
enum tracey_token_type
{
    TRACEY_TOKEN_EOF = 0,
    TRACEY_TOKEN_IDENTIFIER,
    TRACEY_TOKEN_INTEGER,
    TRACEY_TOKEN_FLOATING,
    TRACEY_TOKEN_STRING,
    TRACEY_TOKEN_KEYWORD_INT,
    TRACEY_TOKEN_KEYWORD_FLOAT,
    TRACEY_TOKEN_KEYWORD_DOUBLE,
    TRACEY_TOKEN_KEYWORD_CASE,
    TRACEY_TOKEN_KEYWORD_DEFAULT,
    TRACEY_TOKEN_KEYWORD_RETURN,
    TRACEY_TOKEN_KEYWORD_VOID,
    TRACEY_TOKEN_KEYWORD_BOOL,
    TRACEY_TOKEN_KEYWORD_CHAR,
    TRACEY_TOKEN_KEYWORD_TRUE,
    TRACEY_TOKEN_KEYWORD_TYPEDEF,
    TRACEY_TOKEN_KEYWORD_FALSE,
    TRACEY_TOKEN_KEYWORD_IF,
    TRACEY_TOKEN_KEYWORD_ELSE,
    TRACEY_TOKEN_KEYWORD_WHILE,
    TRACEY_TOKEN_KEYWORD_FOR,
    TRACEY_TOKEN_KEYWORD_BREAK,
    TRACEY_TOKEN_KEYWORD_CONTINUE,
    TRACEY_TOKEN_KEYWORD_DO,
    TRACEY_TOKEN_KEYWORD_GOTO,
    TRACEY_TOKEN_KEYWORD_LONG,
    TRACEY_TOKEN_KEYWORD_SHORT,
    TRACEY_TOKEN_KEYWORD_STRUCT,
    TRACEY_TOKEN_KEYWORD_SWITCH,
    TRACEY_TOKEN_KEYWORD_SIZEOF,
    TRACEY_TOKEN_KEYWORD_UNSIGNED,
    TRACEY_TOKEN_KEYWORD_SIGNED,
    TRACEY_TOKEN_KEYWORD_SIZE_T,
    TRACEY_TOKEN_LPAREN,
    TRACEY_TOKEN_RPAREN,
    TRACEY_TOKEN_LBRACE,
    TRACEY_TOKEN_RBRACE,
    TRACEY_TOKEN_LBRACKET,
    TRACEY_TOKEN_RBRACKET,
    TRACEY_TOKEN_SEMICOLON,
    TRACEY_TOKEN_COMMA,
    TRACEY_TOKEN_DOT,
    TRACEY_TOKEN_ASSIGN,
    TRACEY_TOKEN_PLUS,
    TRACEY_TOKEN_PLUS_PLUS,
    TRACEY_TOKEN_MINUS,
    TRACEY_TOKEN_MINUS_MINUS,
    TRACEY_TOKEN_STAR,
    TRACEY_TOKEN_AMPERSAND,
    TRACEY_TOKEN_SLASH,
    TRACEY_TOKEN_PERCENT,
    TRACEY_TOKEN_BANG,
    TRACEY_TOKEN_LESS,
    TRACEY_TOKEN_GREATER,
    TRACEY_TOKEN_LESS_EQUAL,
    TRACEY_TOKEN_GREATER_EQUAL,
    TRACEY_TOKEN_EQUAL_EQUAL,
    TRACEY_TOKEN_BANG_EQUAL,
    TRACEY_TOKEN_AMP_AMP,
    TRACEY_TOKEN_PIPE_PIPE,
    TRACEY_TOKEN_PIPE,
    TRACEY_TOKEN_CARET,
    TRACEY_TOKEN_TILDE,
    TRACEY_TOKEN_LESS_LESS,
    TRACEY_TOKEN_GREATER_GREATER,
    TRACEY_TOKEN_PLUS_EQUAL,
    TRACEY_TOKEN_MINUS_EQUAL,
    TRACEY_TOKEN_STAR_EQUAL,
    TRACEY_TOKEN_SLASH_EQUAL,
    TRACEY_TOKEN_PERCENT_EQUAL,
    TRACEY_TOKEN_LESS_LESS_EQUAL,
    TRACEY_TOKEN_GREATER_GREATER_EQUAL,
    TRACEY_TOKEN_AMPERSAND_EQUAL,
    TRACEY_TOKEN_PIPE_EQUAL,
    TRACEY_TOKEN_CARET_EQUAL,
    TRACEY_TOKEN_QUESTION,
    TRACEY_TOKEN_COLON,
    TRACEY_TOKEN_CHAR,
    TRACEY_TOKEN_DEFINED,
    TRACEY_TOKEN_ERROR,
};

/* Error kinds carried by TRACEY_TOKEN_ERROR tokens. */
enum tracey_token_error
{
    TRACEY_TOKEN_ERROR_NONE = 0,
    TRACEY_TOKEN_ERROR_INVALID_CHARACTER,
    TRACEY_TOKEN_ERROR_INVALID_NUMBER_SUFFIX,
    TRACEY_TOKEN_ERROR_INVALID_ESCAPE,
    TRACEY_TOKEN_ERROR_INVALID_CHAR_ESCAPE,
    TRACEY_TOKEN_ERROR_UNTERMINATED_STRING,
    TRACEY_TOKEN_ERROR_UNTERMINATED_CHAR,
    TRACEY_TOKEN_ERROR_UNTERMINATED_COMMENT,
};

/*
 * Lexical token.
 *
 * lexeme points into the source buffer supplied to the lexer and is not
 * NUL-terminated. lexeme_len gives its length in bytes. The lexeme is
 * valid only while that source buffer remains alive.
 */
struct tracey_token
{
    enum tracey_token_type type;
    enum tracey_token_error error;
    const char *lexeme;
    size_t lexeme_len;
    size_t line;
    size_t column;
};

/*
 * Movable cursor over a source buffer.
 *
 * content points to a buffer of content_len bytes that must remain valid
 * for the lifetime of the lexer. peeked and has_peeked implement a
 * one-token lookahead cache.
 */
struct tracey_lexer
{
    const char *content;
    size_t content_len;
    size_t position;
    size_t line;
    size_t column;
    struct tracey_token peeked;
    bool has_peeked;
};

/*
 * tracey_lexer_init - initialize a lexer over a source buffer.
 *
 * content may be NULL only when content_len is zero. The buffer is not
 * copied and must outlive the lexer.
 */
void tracey_lexer_init(struct tracey_lexer *lexer, const char *content, size_t content_len);

/*
 * tracey_lexer_next - consume and return the next token.
 */
struct tracey_token tracey_lexer_next(struct tracey_lexer *lexer);

/*
 * tracey_lexer_peek - return the next token without consuming it.
 */
struct tracey_token tracey_lexer_peek(struct tracey_lexer *lexer);

/*
 * tracey_lexer_is_at_end - report whether all input has been consumed.
 */
bool tracey_lexer_is_at_end(const struct tracey_lexer *lexer);

/*
 * tracey_token_is_error - report whether a token represents a lexer error.
 */
bool tracey_token_is_error(const struct tracey_token *token);

/*
 * tracey_token_type_string - return a stable name for a token type.
 */
const char *tracey_token_type_string(enum tracey_token_type type);

/*
 * tracey_token_error_string - return a human-readable error message.
 */
const char *tracey_token_error_string(enum tracey_token_error error);

/*
 * tracey_token_print_debug - print token debug information.
 */
void tracey_token_print_debug(const struct tracey_token *token, FILE *out);

/*
 * tracey_lexer_print_debug - print lexer cursor debug information.
 */
void tracey_lexer_print_debug(const struct tracey_lexer *lexer, FILE *out);

/*
 * tracey_token_print_error - print a one-line diagnostic for an error token.
 *
 * path is the source file name shown in the diagnostic. The token's
 * lexeme bytes that are not printable ASCII are rendered as escapes.
 */
void tracey_token_print_error(const struct tracey_token *token, const char *path, FILE *out);

#ifdef __cplusplus
}
#endif

#endif /* TRACEY_LEXER_H */
