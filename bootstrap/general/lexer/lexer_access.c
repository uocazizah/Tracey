#include <tracey/lexer.h>
#include "lexer_internal.h"

bool tracey_lexer_is_at_end(const tracey_lexer_t* lexer)
{
    if (!lexer) return true;
    return lexer->position >= lexer->length;
}

size_t tracey_lexer_line(const tracey_lexer_t* lexer)
{
    return lexer ? lexer->line : 0;
}

size_t tracey_lexer_column(const tracey_lexer_t* lexer)
{
    return lexer ? lexer->column : 0;
}

const char* tracey_token_type_str(tracey_token_type_t type)
{
    switch (type) {
        case TRACEY_TOKEN_EOF:            return "EOF";
        case TRACEY_TOKEN_IDENTIFIER:     return "IDENTIFIER";
        case TRACEY_TOKEN_INTEGER:        return "INTEGER";
        case TRACEY_TOKEN_KEYWORD_INT:    return "KEYWORD_INT";
        case TRACEY_TOKEN_KEYWORD_RETURN: return "KEYWORD_RETURN";
        case TRACEY_TOKEN_LPAREN:         return "LPAREN";
        case TRACEY_TOKEN_RPAREN:         return "RPAREN";
        case TRACEY_TOKEN_LBRACE:         return "LBRACE";
        case TRACEY_TOKEN_RBRACE:         return "RBRACE";
        case TRACEY_TOKEN_SEMICOLON:      return "SEMICOLON";
        case TRACEY_TOKEN_ERROR:          return "ERROR";
        default:                          return "UNKNOWN";
    }
}