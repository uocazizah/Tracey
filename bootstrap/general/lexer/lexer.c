#define _POSIX_C_SOURCE 200809L

#include <tracey/lexer.h>
#include "lexer_internal.h"
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

/* Keywords table */
static const struct {
    const char* keyword;
    size_t length;
    tracey_token_type_t type;
} keywords[] = {
    {"int",    3, TRACEY_TOKEN_KEYWORD_INT},
    {"return", 6, TRACEY_TOKEN_KEYWORD_RETURN},
};

static const size_t KEYWORD_COUNT = sizeof(keywords) / sizeof(keywords[0]);

/* Forward declarations */
static tracey_token_t make_token(tracey_lexer_t* lexer, tracey_token_type_t type, size_t start_pos, size_t length);
static tracey_token_t make_error_token(tracey_lexer_t* lexer, const char* message);
static bool is_at_end(const tracey_lexer_t* lexer);
static char peek(const tracey_lexer_t* lexer);
static char peek_next(const tracey_lexer_t* lexer);
static char advance(tracey_lexer_t* lexer);
static bool match(tracey_lexer_t* lexer, char expected);
static void skip_whitespace(tracey_lexer_t* lexer);
static tracey_token_t scan_token(tracey_lexer_t* lexer);
static tracey_token_t scan_identifier(tracey_lexer_t* lexer, size_t start_pos);
static tracey_token_t scan_number(tracey_lexer_t* lexer, size_t start_pos);
static tracey_token_type_t check_keyword(const char* start, size_t length);

tracey_lexer_t* tracey_lexer_create(const char* content, size_t length)
{
    if (!content) return NULL;

    tracey_lexer_t* lexer = malloc(sizeof(tracey_lexer_t));
    if (!lexer) return NULL;

    lexer->content = content;
    lexer->length = length;
    lexer->position = 0;
    lexer->line = 1;
    lexer->column = 1;
    lexer->has_peeked = false;
    memset(&lexer->peeked_token, 0, sizeof(tracey_token_t));

    return lexer;
}

void tracey_lexer_free(tracey_lexer_t* lexer)
{
    if (!lexer) return;
    free(lexer);
}

tracey_token_t tracey_lexer_next_token(tracey_lexer_t* lexer)
{
    if (!lexer) {
        tracey_token_t token = {0};
        token.type = TRACEY_TOKEN_ERROR;
        return token;
    }

    /* Return peeked token if available */
    if (lexer->has_peeked) {
        lexer->has_peeked = false;
        return lexer->peeked_token;
    }

    return scan_token(lexer);
}

tracey_token_t tracey_lexer_peek_token(tracey_lexer_t* lexer)
{
    if (!lexer) {
        tracey_token_t token = {0};
        token.type = TRACEY_TOKEN_ERROR;
        return token;
    }

    if (!lexer->has_peeked) {
        lexer->peeked_token = scan_token(lexer);
        lexer->has_peeked = true;
    }

    return lexer->peeked_token;
}

/* Internal helper functions */

static bool is_at_end(const tracey_lexer_t* lexer)
{
    return lexer->position >= lexer->length;
}

static char peek(const tracey_lexer_t* lexer)
{
    if (is_at_end(lexer)) return '\0';
    return lexer->content[lexer->position];
}

static char peek_next(const tracey_lexer_t* lexer)
{
    if (lexer->position + 1 >= lexer->length) return '\0';
    return lexer->content[lexer->position + 1];
}

static char advance(tracey_lexer_t* lexer)
{
    if (is_at_end(lexer)) return '\0';

    char c = lexer->content[lexer->position];
    lexer->position++;

    if (c == '\n') {
        lexer->line++;
        lexer->column = 1;
    } else {
        lexer->column++;
    }

    return c;
}

static bool match(tracey_lexer_t* lexer, char expected)
{
    if (is_at_end(lexer)) return false;
    if (lexer->content[lexer->position] != expected) return false;

    advance(lexer);
    return true;
}

static void skip_whitespace(tracey_lexer_t* lexer)
{
    while (!is_at_end(lexer)) {
        char c = peek(lexer);
        if (c == ' ' || c == '\r' || c == '\t') {
            advance(lexer);
        } else if (c == '\n') {
            advance(lexer);
        } else {
            break;
        }
    }
}

static tracey_token_t scan_token(tracey_lexer_t* lexer)
{
    skip_whitespace(lexer);

    if (is_at_end(lexer)) {
        return make_token(lexer, TRACEY_TOKEN_EOF, lexer->position, 0);
    }

    size_t start_pos = lexer->position;
    char c = advance(lexer);

    switch (c) {
        case '(': return make_token(lexer, TRACEY_TOKEN_LPAREN, start_pos, 1);
        case ')': return make_token(lexer, TRACEY_TOKEN_RPAREN, start_pos, 1);
        case '{': return make_token(lexer, TRACEY_TOKEN_LBRACE, start_pos, 1);
        case '}': return make_token(lexer, TRACEY_TOKEN_RBRACE, start_pos, 1);
        case ';': return make_token(lexer, TRACEY_TOKEN_SEMICOLON, start_pos, 1);

        case '0': case '1': case '2': case '3': case '4':
        case '5': case '6': case '7': case '8': case '9':
            return scan_number(lexer, start_pos);

        default:
            if (isalpha(c) || c == '_') {
                return scan_identifier(lexer, start_pos);
            }
            return make_error_token(lexer, "Unexpected character");
    }
}

static tracey_token_t scan_identifier(tracey_lexer_t* lexer, size_t start_pos)
{
    while (!is_at_end(lexer)) {
        char c = peek(lexer);
        if (isalnum(c) || c == '_') {
            advance(lexer);
        } else {
            break;
        }
    }

    size_t length = lexer->position - start_pos;
    tracey_token_type_t type = check_keyword(lexer->content + start_pos, length);

    if (type == TRACEY_TOKEN_IDENTIFIER) {
        return make_token(lexer, TRACEY_TOKEN_IDENTIFIER, start_pos, length);
    }

    return make_token(lexer, type, start_pos, length);
}

static tracey_token_t scan_number(tracey_lexer_t* lexer, size_t start_pos)
{
    while (!is_at_end(lexer)) {
        char c = peek(lexer);
        if (isdigit(c)) {
            advance(lexer);
        } else {
            break;
        }
    }

    size_t length = lexer->position - start_pos;
    return make_token(lexer, TRACEY_TOKEN_INTEGER, start_pos, length);
}

static tracey_token_type_t check_keyword(const char* start, size_t length)
{
    for (size_t i = 0; i < KEYWORD_COUNT; i++) {
        if (keywords[i].length == length &&
            strncmp(keywords[i].keyword, start, length) == 0) {
            return keywords[i].type;
        }
    }
    return TRACEY_TOKEN_IDENTIFIER;
}

static tracey_token_t make_token(tracey_lexer_t* lexer, tracey_token_type_t type, size_t start_pos, size_t length)
{
    tracey_token_t token = {0};
    token.type = type;
    token.start = lexer->content + start_pos;
    token.length = length;

    /* Calculate line/column for the token start */
    /* We need to scan from beginning to find line/column of start_pos */
    size_t line = 1;
    size_t column = 1;

    for (size_t i = 0; i < start_pos && i < lexer->length; i++) {
        if (lexer->content[i] == '\n') {
            line++;
            column = 1;
        } else {
            column++;
        }
    }

    token.line = line;
    token.column = column;

    return token;
}

static tracey_token_t make_error_token(tracey_lexer_t* lexer, const char* message)
{
    (void)message; /* Unused for now, could be extended */
    return make_token(lexer, TRACEY_TOKEN_ERROR, lexer->position, 0);
}