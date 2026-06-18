#ifndef LEXER_STRUCT_H
#define LEXER_STRUCT_H

typedef enum
{
    TOKEN_EOF,
    TOKEN_ERROR,

    TOKEN_IDENTIFIER,
    TOKEN_INTEGER,

    TOKEN_KW_INT,
    TOKEN_KW_RETURN,

    TOKEN_LPAREN,
    TOKEN_RPAREN,

    TOKEN_LBRACE,
    TOKEN_RBRACE,

    TOKEN_SEMICOLON

} TokenType;

typedef struct
{
    TokenType type;

    const char *start;
    size_t length;

    size_t offset;

    int line;
    int column;

} Token;

typedef struct
{
    const char *source;

    const char *start;
    const char *current;
    const char *end;

    size_t offset;

    int line;
    int column;

} Lexer;

#endif /* LEXER_STRUCT_H */
