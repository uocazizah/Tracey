#include "tracey/lexer.h"

#include "tracey/config.h"

#include <ctype.h>
#include <string.h>

/*
 * Character classification helpers.
 *
 * These avoid locale-dependent behavior from <ctype.h> for identifier
 * rules and keep classification explicit.
 */
static bool tracey_is_ident_start(char c)
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
}

static bool tracey_is_ident_cont(char c)
{
    return tracey_is_ident_start(c) || (c >= '0' && c <= '9');
}

static bool tracey_is_digit(char c)
{
    return c >= '0' && c <= '9';
}

static bool tracey_is_whitespace(char c)
{
    return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\v' || c == '\f';
}

static bool tracey_is_valid_escape(char c)
{
    return c == 'n' || c == 't' || c == 'r' || c == '\\' || c == '"' || c == '0' || c == '\'';
}

struct tracey_keyword_entry
{
    const char *text;
    enum tracey_token_type type;
};

static const struct tracey_keyword_entry tracey_keywords[] = {
    {"bool", TRACEY_TOKEN_KEYWORD_BOOL},
    {"break", TRACEY_TOKEN_KEYWORD_BREAK},
    {"char", TRACEY_TOKEN_KEYWORD_CHAR},
    {"continue", TRACEY_TOKEN_KEYWORD_CONTINUE},
    {"double", TRACEY_TOKEN_KEYWORD_DOUBLE},
    {"else", TRACEY_TOKEN_KEYWORD_ELSE},
    {"false", TRACEY_TOKEN_KEYWORD_FALSE},
    {"float", TRACEY_TOKEN_KEYWORD_FLOAT},
    {"for", TRACEY_TOKEN_KEYWORD_FOR},
    {"goto", TRACEY_TOKEN_KEYWORD_GOTO},
    {"if", TRACEY_TOKEN_KEYWORD_IF},
    {"int", TRACEY_TOKEN_KEYWORD_INT},
    {"long", TRACEY_TOKEN_KEYWORD_LONG},
    {"return", TRACEY_TOKEN_KEYWORD_RETURN},
    {"short", TRACEY_TOKEN_KEYWORD_SHORT},
    {"signed", TRACEY_TOKEN_KEYWORD_SIGNED},
    {"size_t", TRACEY_TOKEN_KEYWORD_SIZE_T},
    {"true", TRACEY_TOKEN_KEYWORD_TRUE},
    {"unsigned", TRACEY_TOKEN_KEYWORD_UNSIGNED},
    {"void", TRACEY_TOKEN_KEYWORD_VOID},
    {"while", TRACEY_TOKEN_KEYWORD_WHILE},
};

void tracey_lexer_init(struct tracey_lexer *lexer, const char *content, size_t content_len)
{
    if (lexer == NULL)
        return;
    lexer->content = content;
    lexer->content_len = content_len;
    lexer->position = 0;
    lexer->line = 1;
    lexer->column = 1;
    lexer->has_peeked = false;
    memset(&lexer->peeked, 0, sizeof(lexer->peeked));
}

static bool tracey_lexer_at_end(const struct tracey_lexer *lexer)
{
    return lexer->position >= lexer->content_len;
}

bool tracey_lexer_is_at_end(const struct tracey_lexer *lexer)
{
    if (lexer == NULL)
        return true;
    return tracey_lexer_at_end(lexer);
}

static char tracey_lexer_current(const struct tracey_lexer *lexer)
{
    if (tracey_lexer_at_end(lexer))
        return '\0';
    return lexer->content[lexer->position];
}

static char tracey_lexer_current_next(const struct tracey_lexer *lexer)
{
    if (lexer->position + 1 >= lexer->content_len)
        return '\0';
    return lexer->content[lexer->position + 1];
}

static char tracey_lexer_advance(struct tracey_lexer *lexer)
{
    char c;

    if (tracey_lexer_at_end(lexer))
        return '\0';

    c = lexer->content[lexer->position];
    lexer->position++;

    if (c == '\n')
    {
        lexer->line++;
        lexer->column = 1;
    }
    else
    {
        lexer->column++;
    }

    return c;
}

static bool tracey_lexer_match(struct tracey_lexer *lexer, char expected)
{
    if (tracey_lexer_at_end(lexer))
        return false;
    if (lexer->content[lexer->position] != expected)
        return false;
    tracey_lexer_advance(lexer);
    return true;
}

static void tracey_lexer_skip_whitespace(struct tracey_lexer *lexer)
{
    while (!tracey_lexer_at_end(lexer) && tracey_is_whitespace(tracey_lexer_current(lexer)))
        tracey_lexer_advance(lexer);
}

static void tracey_lexer_skip_line_comment(struct tracey_lexer *lexer)
{
    tracey_lexer_advance(lexer); /* '/' */
    tracey_lexer_advance(lexer); /* '/' */
    while (!tracey_lexer_at_end(lexer) && tracey_lexer_current(lexer) != '\n')
        tracey_lexer_advance(lexer);
}

static bool tracey_lexer_skip_block_comment(struct tracey_lexer *lexer)
{
    tracey_lexer_advance(lexer); /* '/' */
    tracey_lexer_advance(lexer); /* '*' */

    while (!tracey_lexer_at_end(lexer))
    {
        if (tracey_lexer_current(lexer) == '*' && tracey_lexer_current_next(lexer) == '/')
        {
            tracey_lexer_advance(lexer); /* '*' */
            tracey_lexer_advance(lexer); /* '/' */
            return true;
        }
        tracey_lexer_advance(lexer);
    }

    return false;
}

static struct tracey_token tracey_make_token(enum tracey_token_type type, size_t start_pos,
                                             size_t length, size_t line, size_t column,
                                             const struct tracey_lexer *lexer)
{
    struct tracey_token tok;
    tok.type = type;
    tok.error = TRACEY_TOKEN_ERROR_NONE;
    tok.lexeme = lexer->content ? (lexer->content + start_pos) : NULL;
    tok.lexeme_len = length;
    tok.line = line;
    tok.column = column;
    return tok;
}

static struct tracey_token tracey_make_error_token(enum tracey_token_error error, size_t start_pos,
                                                   size_t length, size_t line, size_t column,
                                                   const struct tracey_lexer *lexer)
{
    struct tracey_token tok = tracey_make_token(TRACEY_TOKEN_ERROR, start_pos, length, line,
                                                column, lexer);
    tok.error = error;
    return tok;
}

static enum tracey_token_type tracey_check_keyword(const char *text, size_t len)
{
    size_t i;
    for (i = 0; i < sizeof(tracey_keywords) / sizeof(tracey_keywords[0]); i++)
    {
        const char *kw = tracey_keywords[i].text;
        if (strlen(kw) == len && memcmp(kw, text, len) == 0)
            return tracey_keywords[i].type;
    }
    return TRACEY_TOKEN_IDENTIFIER;
}

static struct tracey_token tracey_scan_identifier(struct tracey_lexer *lexer, size_t start_pos,
                                                  size_t start_line, size_t start_column)
{
    size_t length;
    enum tracey_token_type type;

    while (!tracey_lexer_at_end(lexer) && tracey_is_ident_cont(tracey_lexer_current(lexer)))
        tracey_lexer_advance(lexer);

    length = lexer->position - start_pos;
    type = tracey_check_keyword(lexer->content + start_pos, length);
    return tracey_make_token(type, start_pos, length, start_line, start_column, lexer);
}

static struct tracey_token tracey_scan_number(struct tracey_lexer *lexer, size_t start_pos,
                                              size_t start_line, size_t start_column)
{
    bool is_float = false;
    bool is_hex = false;
    bool is_bin = false;
    size_t length;

    /* Hexadecimal (0x) or binary (0b) prefix. The leading '0' was already
     * consumed by the caller, so the current character is 'x'/'X'/'b'/'B'. */
    if (!tracey_lexer_at_end(lexer))
    {
        char next = tracey_lexer_current(lexer);
        if (next == 'x' || next == 'X')
        {
            is_hex = true;
            tracey_lexer_advance(lexer); /* 'x' or 'X' */
        }
        else if (next == 'b' || next == 'B')
        {
            is_bin = true;
            tracey_lexer_advance(lexer); /* 'b' or 'B' */
        }
    }

    while (!tracey_lexer_at_end(lexer))
    {
        char c = tracey_lexer_current(lexer);
        bool is_digit = tracey_is_digit(c);

        if (is_hex)
            is_digit = is_digit || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
        else if (is_bin)
            is_digit = (c == '0' || c == '1');

        if (!is_digit)
            break;
        tracey_lexer_advance(lexer);
    }

    if (!tracey_lexer_at_end(lexer) && tracey_lexer_current(lexer) == '.')
    {
        is_float = true;
        tracey_lexer_advance(lexer);
        while (!tracey_lexer_at_end(lexer) && tracey_is_digit(tracey_lexer_current(lexer)))
            tracey_lexer_advance(lexer);
    }

    if (!tracey_lexer_at_end(lexer) &&
        (tracey_lexer_current(lexer) == 'e' || tracey_lexer_current(lexer) == 'E'))
    {
        is_float = true;
        tracey_lexer_advance(lexer);
        if (!tracey_lexer_at_end(lexer) &&
            (tracey_lexer_current(lexer) == '+' || tracey_lexer_current(lexer) == '-'))
        {
            tracey_lexer_advance(lexer);
        }
        while (!tracey_lexer_at_end(lexer) && tracey_is_digit(tracey_lexer_current(lexer)))
            tracey_lexer_advance(lexer);
    }

    if (!tracey_lexer_at_end(lexer) &&
        (tracey_lexer_current(lexer) == 'f' || tracey_lexer_current(lexer) == 'F'))
    {
        is_float = true;
        tracey_lexer_advance(lexer);
    }

    if (!tracey_lexer_at_end(lexer) && tracey_is_ident_start(tracey_lexer_current(lexer)))
    {
        while (!tracey_lexer_at_end(lexer) && tracey_is_ident_cont(tracey_lexer_current(lexer)))
            tracey_lexer_advance(lexer);
        TRACEY_TRACE("[lexer] invalid number suffix at %zu:%zu\n", start_line, start_column);
        return tracey_make_error_token(TRACEY_TOKEN_ERROR_INVALID_NUMBER_SUFFIX, start_pos,
                                       lexer->position - start_pos, start_line, start_column,
                                       lexer);
    }

    length = lexer->position - start_pos;
    return tracey_make_token(is_float ? TRACEY_TOKEN_FLOATING : TRACEY_TOKEN_INTEGER, start_pos,
                             length, start_line, start_column, lexer);
}

static struct tracey_token tracey_scan_string(struct tracey_lexer *lexer, size_t start_pos,
                                              size_t start_line, size_t start_column)
{
    size_t bad_escape_pos = 0;
    size_t bad_escape_line = 0;
    size_t bad_escape_column = 0;
    bool has_bad_escape = false;

    while (!tracey_lexer_at_end(lexer))
    {
        char c = tracey_lexer_current(lexer);

        if (c == '"')
        {
            tracey_lexer_advance(lexer);
            if (has_bad_escape)
            {
                return tracey_make_error_token(TRACEY_TOKEN_ERROR_INVALID_ESCAPE, bad_escape_pos, 2,
                                               bad_escape_line, bad_escape_column, lexer);
            }
            return tracey_make_token(TRACEY_TOKEN_STRING, start_pos, lexer->position - start_pos,
                                     start_line, start_column, lexer);
        }

        if (c == '\n')
            break;

        if (c == '\\')
        {
            size_t escape_pos = lexer->position;
            size_t escape_line = lexer->line;
            size_t escape_column = lexer->column;
            tracey_lexer_advance(lexer);
            if (tracey_lexer_at_end(lexer) || tracey_lexer_current(lexer) == '\n')
                break;
            if (!has_bad_escape && !tracey_is_valid_escape(tracey_lexer_current(lexer)))
            {
                has_bad_escape = true;
                bad_escape_pos = escape_pos;
                bad_escape_line = escape_line;
                bad_escape_column = escape_column;
            }
            tracey_lexer_advance(lexer);
            continue;
        }

        tracey_lexer_advance(lexer);
    }

    return tracey_make_error_token(TRACEY_TOKEN_ERROR_UNTERMINATED_STRING, start_pos,
                                   lexer->position - start_pos, start_line, start_column, lexer);
}

static struct tracey_token tracey_scan_char(struct tracey_lexer *lexer, size_t start_pos,
                                            size_t start_line, size_t start_column)
{
    size_t bad_escape_pos = 0;
    size_t bad_escape_line = 0;
    size_t bad_escape_column = 0;
    bool has_bad_escape = false;

    while (!tracey_lexer_at_end(lexer))
    {
        char c = tracey_lexer_current(lexer);

        if (c == '\'')
        {
            tracey_lexer_advance(lexer);
            if (has_bad_escape)
            {
                return tracey_make_error_token(TRACEY_TOKEN_ERROR_INVALID_CHAR_ESCAPE,
                                               bad_escape_pos, 2, bad_escape_line,
                                               bad_escape_column, lexer);
            }
            return tracey_make_token(TRACEY_TOKEN_CHAR, start_pos, lexer->position - start_pos,
                                     start_line, start_column, lexer);
        }

        if (c == '\n')
            break;

        if (c == '\\')
        {
            size_t escape_pos = lexer->position;
            size_t escape_line = lexer->line;
            size_t escape_column = lexer->column;
            tracey_lexer_advance(lexer);
            if (tracey_lexer_at_end(lexer) || tracey_lexer_current(lexer) == '\n')
                break;
            if (!has_bad_escape && !tracey_is_valid_escape(tracey_lexer_current(lexer)))
            {
                has_bad_escape = true;
                bad_escape_pos = escape_pos;
                bad_escape_line = escape_line;
                bad_escape_column = escape_column;
            }
            tracey_lexer_advance(lexer);
            continue;
        }

        tracey_lexer_advance(lexer);
    }

    return tracey_make_error_token(TRACEY_TOKEN_ERROR_UNTERMINATED_CHAR, start_pos,
                                   lexer->position - start_pos, start_line, start_column, lexer);
}

static struct tracey_token tracey_scan_token(struct tracey_lexer *lexer)
{
    size_t start_pos;
    size_t start_line;
    size_t start_column;
    char c;

    for (;;)
    {
        tracey_lexer_skip_whitespace(lexer);
        if (tracey_lexer_at_end(lexer))
            break;

        if (tracey_lexer_current(lexer) == '/' && tracey_lexer_current_next(lexer) == '/')
        {
            tracey_lexer_skip_line_comment(lexer);
            continue;
        }
        if (tracey_lexer_current(lexer) == '/' && tracey_lexer_current_next(lexer) == '*')
        {
            size_t comment_pos = lexer->position;
            size_t comment_line = lexer->line;
            size_t comment_column = lexer->column;
            if (!tracey_lexer_skip_block_comment(lexer))
            {
                return tracey_make_error_token(TRACEY_TOKEN_ERROR_UNTERMINATED_COMMENT, comment_pos,
                                               lexer->position - comment_pos, comment_line,
                                               comment_column, lexer);
            }
            continue;
        }
        break;
    }

    start_pos = lexer->position;
    start_line = lexer->line;
    start_column = lexer->column;

    if (tracey_lexer_at_end(lexer))
        return tracey_make_token(TRACEY_TOKEN_EOF, start_pos, 0, start_line, start_column, lexer);

    c = tracey_lexer_advance(lexer);

    switch (c)
    {
        case '(':
            return tracey_make_token(TRACEY_TOKEN_LPAREN, start_pos, 1, start_line, start_column, lexer);
        case ')':
            return tracey_make_token(TRACEY_TOKEN_RPAREN, start_pos, 1, start_line, start_column, lexer);
        case '{':
            return tracey_make_token(TRACEY_TOKEN_LBRACE, start_pos, 1, start_line, start_column, lexer);
        case '}':
            return tracey_make_token(TRACEY_TOKEN_RBRACE, start_pos, 1, start_line, start_column, lexer);
        case '[':
            return tracey_make_token(TRACEY_TOKEN_LBRACKET, start_pos, 1, start_line, start_column, lexer);
        case ']':
            return tracey_make_token(TRACEY_TOKEN_RBRACKET, start_pos, 1, start_line, start_column, lexer);
        case ';':
            return tracey_make_token(TRACEY_TOKEN_SEMICOLON, start_pos, 1, start_line, start_column, lexer);
        case ',':
            return tracey_make_token(TRACEY_TOKEN_COMMA, start_pos, 1, start_line, start_column, lexer);
        case '.':
            return tracey_make_token(TRACEY_TOKEN_DOT, start_pos, 1, start_line, start_column, lexer);
        case '+':
            return tracey_make_token(tracey_lexer_match(lexer, '=') ? TRACEY_TOKEN_PLUS_EQUAL
                                                                    : TRACEY_TOKEN_PLUS,
                                     start_pos, lexer->position - start_pos, start_line,
                                     start_column, lexer);
        case '-':
            return tracey_make_token(tracey_lexer_match(lexer, '=') ? TRACEY_TOKEN_MINUS_EQUAL
                                                                    : TRACEY_TOKEN_MINUS,
                                     start_pos, lexer->position - start_pos, start_line,
                                     start_column, lexer);
        case '*':
            return tracey_make_token(tracey_lexer_match(lexer, '=') ? TRACEY_TOKEN_STAR_EQUAL
                                                                    : TRACEY_TOKEN_STAR,
                                     start_pos, lexer->position - start_pos, start_line,
                                     start_column, lexer);
        case '%':
            return tracey_make_token(tracey_lexer_match(lexer, '=') ? TRACEY_TOKEN_PERCENT_EQUAL
                                                                    : TRACEY_TOKEN_PERCENT,
                                     start_pos, lexer->position - start_pos, start_line,
                                     start_column, lexer);
        case '/':
            return tracey_make_token(tracey_lexer_match(lexer, '=') ? TRACEY_TOKEN_SLASH_EQUAL
                                                                    : TRACEY_TOKEN_SLASH,
                                     start_pos, lexer->position - start_pos, start_line,
                                     start_column, lexer);
        case '=':
            return tracey_make_token(tracey_lexer_match(lexer, '=') ? TRACEY_TOKEN_EQUAL_EQUAL
                                                                    : TRACEY_TOKEN_ASSIGN,
                                     start_pos, lexer->position - start_pos, start_line,
                                     start_column, lexer);
        case '!':
            return tracey_make_token(tracey_lexer_match(lexer, '=') ? TRACEY_TOKEN_BANG_EQUAL
                                                                    : TRACEY_TOKEN_BANG,
                                     start_pos, lexer->position - start_pos, start_line,
                                     start_column, lexer);
        case '<':
            if (tracey_lexer_match(lexer, '<'))
            {
                if (tracey_lexer_match(lexer, '='))
                    return tracey_make_token(TRACEY_TOKEN_LESS_LESS_EQUAL,
                                             start_pos, lexer->position - start_pos,
                                             start_line, start_column, lexer);
                return tracey_make_token(TRACEY_TOKEN_LESS_LESS,
                                         start_pos, lexer->position - start_pos,
                                         start_line, start_column, lexer);
            }
            return tracey_make_token(tracey_lexer_match(lexer, '=') ? TRACEY_TOKEN_LESS_EQUAL
                                                                    : TRACEY_TOKEN_LESS,
                                     start_pos, lexer->position - start_pos, start_line,
                                     start_column, lexer);
        case '>':
            if (tracey_lexer_match(lexer, '>'))
            {
                if (tracey_lexer_match(lexer, '='))
                    return tracey_make_token(TRACEY_TOKEN_GREATER_GREATER_EQUAL,
                                             start_pos, lexer->position - start_pos,
                                             start_line, start_column, lexer);
                return tracey_make_token(TRACEY_TOKEN_GREATER_GREATER,
                                         start_pos, lexer->position - start_pos,
                                         start_line, start_column, lexer);
            }
            return tracey_make_token(tracey_lexer_match(lexer, '=') ? TRACEY_TOKEN_GREATER_EQUAL
                                                                    : TRACEY_TOKEN_GREATER,
                                     start_pos, lexer->position - start_pos, start_line,
                                     start_column, lexer);
        case '&':
            if (tracey_lexer_match(lexer, '&'))
                return tracey_make_token(TRACEY_TOKEN_AMP_AMP, start_pos, lexer->position - start_pos,
                                         start_line, start_column, lexer);
            if (tracey_lexer_match(lexer, '='))
                return tracey_make_token(TRACEY_TOKEN_AMPERSAND_EQUAL,
                                         start_pos, lexer->position - start_pos,
                                         start_line, start_column, lexer);
            return tracey_make_token(TRACEY_TOKEN_AMPERSAND, start_pos, 1, start_line, start_column, lexer);
        case '|':
            if (tracey_lexer_match(lexer, '|'))
                return tracey_make_token(TRACEY_TOKEN_PIPE_PIPE, start_pos, lexer->position - start_pos,
                                         start_line, start_column, lexer);
            if (tracey_lexer_match(lexer, '='))
                return tracey_make_token(TRACEY_TOKEN_PIPE_EQUAL,
                                         start_pos, lexer->position - start_pos,
                                         start_line, start_column, lexer);
            return tracey_make_token(TRACEY_TOKEN_PIPE, start_pos, 1, start_line, start_column, lexer);
        case '^':
            if (tracey_lexer_match(lexer, '='))
                return tracey_make_token(TRACEY_TOKEN_CARET_EQUAL,
                                         start_pos, lexer->position - start_pos,
                                         start_line, start_column, lexer);
            return tracey_make_token(TRACEY_TOKEN_CARET, start_pos, 1, start_line, start_column, lexer);
        case '~':
            return tracey_make_token(TRACEY_TOKEN_TILDE, start_pos, 1, start_line, start_column, lexer);
        case '?':
            return tracey_make_token(TRACEY_TOKEN_QUESTION, start_pos, 1, start_line, start_column, lexer);
        case ':':
            return tracey_make_token(TRACEY_TOKEN_COLON, start_pos, 1, start_line, start_column, lexer);
        case '\'':
            return tracey_scan_char(lexer, start_pos, start_line, start_column);
        case '"':
            return tracey_scan_string(lexer, start_pos, start_line, start_column);
        default:
            if (tracey_is_digit(c))
                return tracey_scan_number(lexer, start_pos, start_line, start_column);
            if (tracey_is_ident_start(c))
                return tracey_scan_identifier(lexer, start_pos, start_line, start_column);
            break;
    }

    TRACEY_TRACE("[lexer] unexpected character '%c' at %zu:%zu\n", c, start_line, start_column);
    return tracey_make_error_token(TRACEY_TOKEN_ERROR_INVALID_CHARACTER, start_pos,
                                   lexer->position - start_pos, start_line, start_column, lexer);
}

struct tracey_token tracey_lexer_next(struct tracey_lexer *lexer)
{
    struct tracey_token tok;

    if (lexer == NULL)
    {
        memset(&tok, 0, sizeof(tok));
        return tok;
    }

    if (lexer->has_peeked)
    {
        tok = lexer->peeked;
        lexer->has_peeked = false;
        return tok;
    }

    return tracey_scan_token(lexer);
}

struct tracey_token tracey_lexer_peek(struct tracey_lexer *lexer)
{
    if (lexer == NULL)
    {
        struct tracey_token tok;
        memset(&tok, 0, sizeof(tok));
        return tok;
    }

    if (!lexer->has_peeked)
    {
        lexer->peeked = tracey_scan_token(lexer);
        lexer->has_peeked = true;
    }

    return lexer->peeked;
}

bool tracey_token_is_error(const struct tracey_token *token)
{
    return token != NULL && token->type == TRACEY_TOKEN_ERROR;
}

const char *tracey_token_type_string(enum tracey_token_type type)
{
    switch (type)
    {
        case TRACEY_TOKEN_EOF:
            return "EOF";
        case TRACEY_TOKEN_IDENTIFIER:
            return "IDENTIFIER";
        case TRACEY_TOKEN_INTEGER:
            return "INTEGER";
        case TRACEY_TOKEN_FLOATING:
            return "FLOATING";
        case TRACEY_TOKEN_STRING:
            return "STRING";
        case TRACEY_TOKEN_KEYWORD_INT:
            return "KEYWORD_INT";
        case TRACEY_TOKEN_KEYWORD_RETURN:
            return "KEYWORD_RETURN";
        case TRACEY_TOKEN_KEYWORD_VOID:
            return "KEYWORD_VOID";
        case TRACEY_TOKEN_KEYWORD_BOOL:
            return "KEYWORD_BOOL";
        case TRACEY_TOKEN_KEYWORD_CHAR:
            return "KEYWORD_CHAR";
        case TRACEY_TOKEN_KEYWORD_FLOAT:
            return "KEYWORD_FLOAT";
        case TRACEY_TOKEN_KEYWORD_DOUBLE:
            return "KEYWORD_DOUBLE";
        case TRACEY_TOKEN_KEYWORD_TRUE:
            return "KEYWORD_TRUE";
        case TRACEY_TOKEN_KEYWORD_FALSE:
            return "KEYWORD_FALSE";
        case TRACEY_TOKEN_KEYWORD_IF:
            return "KEYWORD_IF";
        case TRACEY_TOKEN_KEYWORD_ELSE:
            return "KEYWORD_ELSE";
        case TRACEY_TOKEN_KEYWORD_WHILE:
            return "KEYWORD_WHILE";
        case TRACEY_TOKEN_KEYWORD_FOR:
            return "KEYWORD_FOR";
        case TRACEY_TOKEN_KEYWORD_BREAK:
            return "KEYWORD_BREAK";
        case TRACEY_TOKEN_KEYWORD_CONTINUE:
            return "KEYWORD_CONTINUE";
        case TRACEY_TOKEN_KEYWORD_GOTO:
            return "KEYWORD_GOTO";
        case TRACEY_TOKEN_KEYWORD_LONG:
            return "KEYWORD_LONG";
        case TRACEY_TOKEN_KEYWORD_SHORT:
            return "KEYWORD_SHORT";
        case TRACEY_TOKEN_KEYWORD_UNSIGNED:
            return "KEYWORD_UNSIGNED";
        case TRACEY_TOKEN_KEYWORD_SIGNED:
            return "KEYWORD_SIGNED";
        case TRACEY_TOKEN_KEYWORD_SIZE_T:
            return "KEYWORD_SIZE_T";
        case TRACEY_TOKEN_LPAREN:
            return "LPAREN";
        case TRACEY_TOKEN_RPAREN:
            return "RPAREN";
        case TRACEY_TOKEN_LBRACE:
            return "LBRACE";
        case TRACEY_TOKEN_RBRACE:
            return "RBRACE";
        case TRACEY_TOKEN_LBRACKET:
            return "LBRACKET";
        case TRACEY_TOKEN_RBRACKET:
            return "RBRACKET";
        case TRACEY_TOKEN_SEMICOLON:
            return "SEMICOLON";
        case TRACEY_TOKEN_COMMA:
            return "COMMA";
        case TRACEY_TOKEN_DOT:
            return "DOT";
        case TRACEY_TOKEN_ASSIGN:
            return "ASSIGN";
        case TRACEY_TOKEN_PLUS:
            return "PLUS";
        case TRACEY_TOKEN_MINUS:
            return "MINUS";
        case TRACEY_TOKEN_STAR:
            return "STAR";
        case TRACEY_TOKEN_SLASH:
            return "SLASH";
        case TRACEY_TOKEN_PERCENT:
            return "PERCENT";
        case TRACEY_TOKEN_BANG:
            return "BANG";
        case TRACEY_TOKEN_LESS:
            return "LESS";
        case TRACEY_TOKEN_GREATER:
            return "GREATER";
        case TRACEY_TOKEN_LESS_EQUAL:
            return "LESS_EQUAL";
        case TRACEY_TOKEN_GREATER_EQUAL:
            return "GREATER_EQUAL";
        case TRACEY_TOKEN_EQUAL_EQUAL:
            return "EQUAL_EQUAL";
        case TRACEY_TOKEN_BANG_EQUAL:
            return "BANG_EQUAL";
        case TRACEY_TOKEN_AMP_AMP:
            return "AMP_AMP";
        case TRACEY_TOKEN_AMPERSAND:
            return "AMPERSAND";
        case TRACEY_TOKEN_PIPE_PIPE:
            return "PIPE_PIPE";
        case TRACEY_TOKEN_PIPE:
            return "PIPE";
        case TRACEY_TOKEN_CARET:
            return "CARET";
        case TRACEY_TOKEN_TILDE:
            return "TILDE";
        case TRACEY_TOKEN_LESS_LESS:
            return "LESS_LESS";
        case TRACEY_TOKEN_GREATER_GREATER:
            return "GREATER_GREATER";
        case TRACEY_TOKEN_PLUS_EQUAL:
            return "PLUS_EQUAL";
        case TRACEY_TOKEN_MINUS_EQUAL:
            return "MINUS_EQUAL";
        case TRACEY_TOKEN_STAR_EQUAL:
            return "STAR_EQUAL";
        case TRACEY_TOKEN_SLASH_EQUAL:
            return "SLASH_EQUAL";
        case TRACEY_TOKEN_PERCENT_EQUAL:
            return "PERCENT_EQUAL";
        case TRACEY_TOKEN_LESS_LESS_EQUAL:
            return "LESS_LESS_EQUAL";
        case TRACEY_TOKEN_GREATER_GREATER_EQUAL:
            return "GREATER_GREATER_EQUAL";
        case TRACEY_TOKEN_AMPERSAND_EQUAL:
            return "AMPERSAND_EQUAL";
        case TRACEY_TOKEN_PIPE_EQUAL:
            return "PIPE_EQUAL";
        case TRACEY_TOKEN_CARET_EQUAL:
            return "CARET_EQUAL";
        case TRACEY_TOKEN_QUESTION:
            return "QUESTION";
        case TRACEY_TOKEN_COLON:
            return "COLON";
        case TRACEY_TOKEN_CHAR:
            return "CHAR";
        case TRACEY_TOKEN_DEFINED:
            return "DEFINED";
        case TRACEY_TOKEN_ERROR:
            return "ERROR";
        default:
            return "UNKNOWN";
    }
}

const char *tracey_token_error_string(enum tracey_token_error error)
{
    switch (error)
    {
        case TRACEY_TOKEN_ERROR_NONE:
            return "No error";
        case TRACEY_TOKEN_ERROR_INVALID_CHARACTER:
            return "Invalid character in source";
        case TRACEY_TOKEN_ERROR_INVALID_NUMBER_SUFFIX:
            return "Invalid numeric literal suffix";
        case TRACEY_TOKEN_ERROR_INVALID_ESCAPE:
            return "Invalid escape sequence in string literal";
        case TRACEY_TOKEN_ERROR_INVALID_CHAR_ESCAPE:
            return "Invalid escape sequence in character literal";
        case TRACEY_TOKEN_ERROR_UNTERMINATED_STRING:
            return "Unterminated string literal";
        case TRACEY_TOKEN_ERROR_UNTERMINATED_CHAR:
            return "Unterminated character literal";
        case TRACEY_TOKEN_ERROR_UNTERMINATED_COMMENT:
            return "Unterminated block comment";
        default:
            return "Unknown lexer error";
    }
}

void tracey_token_print_debug(const struct tracey_token *token, FILE *out)
{
    if (token == NULL || out == NULL)
        return;
    fprintf(out, "Token(type=%s, line=%zu, column=%zu, lexeme=\"",
            tracey_token_type_string(token->type), token->line, token->column);
    if (token->lexeme != NULL)
        fwrite(token->lexeme, 1, token->lexeme_len, out);
    fprintf(out, "\")\n");
}

void tracey_lexer_print_debug(const struct tracey_lexer *lexer, FILE *out)
{
    if (lexer == NULL || out == NULL)
        return;
    fprintf(out, "Lexer(position=%zu, line=%zu, column=%zu, has_peeked=%s)\n",
            lexer->position, lexer->line, lexer->column,
            lexer->has_peeked ? "true" : "false");
}

void tracey_token_print_error(const struct tracey_token *token, const char *path, FILE *out)
{
    size_t i;

    if (token == NULL || out == NULL)
        return;
    if (path == NULL)
        path = "<input>";

    fprintf(out, "Error: %s:%zu:%zu: %s", path, token->line, token->column,
            tracey_token_error_string(token->error));

    if (token->lexeme != NULL && token->lexeme_len > 0)
    {
        fprintf(out, " near '");
        for (i = 0; i < token->lexeme_len; i++)
        {
            unsigned char c = (unsigned char)token->lexeme[i];
            if (c == '\n')
                fprintf(out, "\\n");
            else if (c == '\t')
                fprintf(out, "\\t");
            else if (c == '\r')
                fprintf(out, "\\r");
            else if (c >= 0x20 && c <= 0x7e)
                fputc(c, out);
            else
                fprintf(out, "\\x%02X", c);
        }
        fprintf(out, "'");
    }

    fprintf(out, "\n");
}
