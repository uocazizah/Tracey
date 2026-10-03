#ifndef TRACEY_LEXER_INTERNAL_H
#define TRACEY_LEXER_INTERNAL_H

#include <tracey/lexer.h>
#include <stddef.h>

struct tracey_lexer {
    const char* content;
    size_t length;
    size_t position;
    size_t line;
    size_t column;
    tracey_token_t peeked_token;
    bool has_peeked;
};

#endif /* TRACEY_LEXER_INTERNAL_H */