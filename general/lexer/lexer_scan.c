#include "tracey/lexer.h"

#include "lexer_internal.h"

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
    {"case", TRACEY_TOKEN_KEYWORD_CASE},
    {"char", TRACEY_TOKEN_KEYWORD_CHAR},
    {"continue", TRACEY_TOKEN_KEYWORD_CONTINUE},
    {"default", TRACEY_TOKEN_KEYWORD_DEFAULT},
    {"do", TRACEY_TOKEN_KEYWORD_DO},
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
    {"struct", TRACEY_TOKEN_KEYWORD_STRUCT},
    {"switch", TRACEY_TOKEN_KEYWORD_SWITCH},
    {"signed", TRACEY_TOKEN_KEYWORD_SIGNED},
    {"size_t", TRACEY_TOKEN_KEYWORD_SIZE_T},
    {"sizeof", TRACEY_TOKEN_KEYWORD_SIZEOF},
    {"true", TRACEY_TOKEN_KEYWORD_TRUE},
    {"typedef", TRACEY_TOKEN_KEYWORD_TYPEDEF},
    {"unsigned", TRACEY_TOKEN_KEYWORD_UNSIGNED},
    {"void", TRACEY_TOKEN_KEYWORD_VOID},
    {"while", TRACEY_TOKEN_KEYWORD_WHILE},
};

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

static bool tracey_is_int_suffix_text(const char *text, size_t len)
{
    char buf[16];
    size_t i;

    if (text == NULL || len == 0 || len >= sizeof(buf))
        return false;

    for (i = 0; i < len; i++)
    {
        char c = text[i];
        if (c >= 'A' && c <= 'Z')
            buf[i] = (char)(c - 'A' + 'a');
        else
            buf[i] = c;
    }
    buf[len] = '\0';

    return strcmp(buf, "u") == 0 || strcmp(buf, "l") == 0 ||
           strcmp(buf, "ul") == 0 || strcmp(buf, "lu") == 0 ||
           strcmp(buf, "ll") == 0 || strcmp(buf, "ull") == 0 ||
           strcmp(buf, "llu") == 0;
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

    /*
     * Integer suffixes (u, l, ul, ll and their uppercase/combined forms)
     * are valid for integer literals. Consume a run of suffix characters
     * and validate it; any other trailing identifier character makes the
     * whole literal an invalid-number-suffix error.
     */
    if (!is_float && !tracey_lexer_at_end(lexer))
    {
        char c = tracey_lexer_current(lexer);
        if (c == 'u' || c == 'U' || c == 'l' || c == 'L')
        {
            size_t suffix_start = lexer->position;
            const char *suffix_text;

            while (!tracey_lexer_at_end(lexer))
            {
                char cc = tracey_lexer_current(lexer);
                if (cc == 'u' || cc == 'U' || cc == 'l' || cc == 'L')
                    tracey_lexer_advance(lexer);
                else
                    break;
            }

            suffix_text = lexer->content + suffix_start;
            if (!tracey_is_int_suffix_text(suffix_text, lexer->position - suffix_start) ||
                (!tracey_lexer_at_end(lexer) && tracey_is_ident_cont(tracey_lexer_current(lexer))))
            {
                while (!tracey_lexer_at_end(lexer) && tracey_is_ident_cont(tracey_lexer_current(lexer)))
                    tracey_lexer_advance(lexer);
                TRACEY_TRACE("[lexer] invalid number suffix at %zu:%zu\n", start_line, start_column);
                return tracey_make_error_token(TRACEY_TOKEN_ERROR_INVALID_NUMBER_SUFFIX, start_pos,
                                               lexer->position - start_pos, start_line, start_column,
                                               lexer);
            }
        }
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

struct tracey_token tracey_scan_token(struct tracey_lexer *lexer)
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
            if (tracey_lexer_match(lexer, '+'))
                return tracey_make_token(TRACEY_TOKEN_PLUS_PLUS, start_pos, lexer->position - start_pos,
                                         start_line, start_column, lexer);
            return tracey_make_token(tracey_lexer_match(lexer, '=') ? TRACEY_TOKEN_PLUS_EQUAL
                                                                     : TRACEY_TOKEN_PLUS,
                                     start_pos, lexer->position - start_pos, start_line,
                                     start_column, lexer);
        case '-':
            if (tracey_lexer_match(lexer, '-'))
                return tracey_make_token(TRACEY_TOKEN_MINUS_MINUS, start_pos, lexer->position - start_pos,
                                         start_line, start_column, lexer);
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
