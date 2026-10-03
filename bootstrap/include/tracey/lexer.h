#ifndef TRACEY_LEXER_H
#define TRACEY_LEXER_H

#include <stddef.h>
#include <stdbool.h>
#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Token types */
typedef enum tracey_token_type {
    TRACEY_TOKEN_EOF = 0,
    TRACEY_TOKEN_IDENTIFIER,
    TRACEY_TOKEN_INTEGER,
    TRACEY_TOKEN_KEYWORD_INT,
    TRACEY_TOKEN_KEYWORD_RETURN,
    TRACEY_TOKEN_LPAREN,
    TRACEY_TOKEN_RPAREN,
    TRACEY_TOKEN_LBRACE,
    TRACEY_TOKEN_RBRACE,
    TRACEY_TOKEN_SEMICOLON,
    TRACEY_TOKEN_ERROR,
} tracey_token_type_t;

/* Token structure */
typedef struct tracey_token {
    tracey_token_type_t type;
    const char* start;
    size_t length;
    size_t line;
    size_t column;
} tracey_token_t;

/* Lexer structure */
typedef struct tracey_lexer tracey_lexer_t;

/* Create a lexer from source content */
tracey_lexer_t* tracey_lexer_create(const char* content, size_t length);

/* Free the lexer */
void tracey_lexer_free(tracey_lexer_t* lexer);

/* Get the next token */
tracey_token_t tracey_lexer_next_token(tracey_lexer_t* lexer);

/* Peek at the next token without consuming it */
tracey_token_t tracey_lexer_peek_token(tracey_lexer_t* lexer);

/* Check if lexer is at end */
bool tracey_lexer_is_at_end(const tracey_lexer_t* lexer);

/* Get current line number */
size_t tracey_lexer_line(const tracey_lexer_t* lexer);

/* Get current column number */
size_t tracey_lexer_column(const tracey_lexer_t* lexer);

/* Token type to string */
const char* tracey_token_type_str(tracey_token_type_t type);

/* Print token debug info */
void tracey_token_print_debug(const tracey_token_t* token, FILE* out);

/* Print lexer debug info */
void tracey_lexer_print_debug(const tracey_lexer_t* lexer, FILE* out);

#ifdef __cplusplus
}
#endif

#endif /* TRACEY_LEXER_H */