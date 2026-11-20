#include "tracey/parser.h"
#include "parser_internal.h"

char *tracey_strdup_n(const char *s, size_t n)
{
    char *out = (char *)malloc(n + 1);
    if (out == NULL)
        return NULL;
    memcpy(out, s, n);
    out[n] = '\0';
    return out;
}


char *tracey_token_strdup(const struct tracey_token *tok)
{
    if (tok == NULL || tok->lexeme == NULL)
        return strdup("");
    return tracey_strdup_n(tok->lexeme, tok->lexeme_len);
}

/*
 * tracey_parse_int_literal - decode an integer token into its numeric value.
 *
 * The lexer emits a single INTEGER token that still carries any 0x/0X or
 * 0b/0B prefix, so the decoder must select the base from the lexeme rather
 * than assume decimal. tok is a borrowed token whose lexeme is valid for the
 * duration of the call.
 *
 * Returns true and stores the decoded magnitude in *out on success. Returns
 * false when the token is empty or contains a digit that is invalid for the
 * selected base; the caller is responsible for reporting a diagnostic. The
 * value wraps modulo 2^64, which matches two's-complement expectations for
 * the AArch64 target.
 */

bool tracey_parse_int_literal(const struct tracey_token *tok, unsigned long long *out)
{
    size_t i = 0;
    unsigned base = 10;
    unsigned long long v = 0;

    if (tok == NULL || tok->lexeme == NULL || tok->lexeme_len == 0 || out == NULL)
        return false;

    if (tok->lexeme_len >= 2 && tok->lexeme[0] == '0' &&
        (tok->lexeme[1] == 'x' || tok->lexeme[1] == 'X'))
    {
        base = 16;
        i = 2;
    }
    else if (tok->lexeme_len >= 2 && tok->lexeme[0] == '0' &&
             (tok->lexeme[1] == 'b' || tok->lexeme[1] == 'B'))
    {
        base = 2;
        i = 2;
    }

    if (i >= tok->lexeme_len)
        return false;

    {
        size_t digits_start = i;

        for (; i < tok->lexeme_len; i++)
        {
            char c = tok->lexeme[i];
            unsigned d;

            /* Stop at a type suffix (u/U/l/L); it is not part of the
             * numeric value. */
            if (c == 'u' || c == 'U' || c == 'l' || c == 'L')
                break;

            if (c >= '0' && c <= '9')
                d = (unsigned)(c - '0');
            else if (c >= 'a' && c <= 'f')
                d = (unsigned)(c - 'a') + 10u;
            else if (c >= 'A' && c <= 'F')
                d = (unsigned)(c - 'A') + 10u;
            else
                return false;

            if (d >= base)
                return false;
            v = v * base + d;
        }

        if (i == digits_start)
            return false;
    }

    *out = v;
    return true;
}


void tracey_parser_advance(struct tracey_parser *parser)
{
    parser->current = tracey_lexer_next(parser->lexer);
    parser->has_current = true;
}


void tracey_parser_init(struct tracey_parser *parser, struct tracey_lexer *lexer)
{
    if (parser == NULL)
        return;
    memset(parser, 0, sizeof(*parser));
    parser->lexer = lexer;
    if (lexer != NULL)
        tracey_parser_advance(parser);
}


void tracey_parser_error(struct tracey_parser *parser, const char *msg, const struct tracey_token *tok)
{
    struct tracey_parse_error *new_errors;
    char *copy;

    if (parser == NULL)
        return;

    parser->had_error = true;

    if (parser->error_count == parser->error_cap)
    {
        size_t new_cap = parser->error_cap ? parser->error_cap * 2 : 8;
        new_errors = (struct tracey_parse_error *)realloc(parser->errors,
                                                          new_cap * sizeof(*new_errors));
        if (new_errors == NULL)
            return;
        parser->errors = new_errors;
        parser->error_cap = new_cap;
    }

    copy = strdup(msg ? msg : "parse error");
    if (copy == NULL)
        return;

    parser->errors[parser->error_count].message = copy;
    parser->errors[parser->error_count].line = tok ? tok->line : 1;
    parser->errors[parser->error_count].column = tok ? tok->column : 1;
    parser->error_count++;
}


void tracey_parser_free(struct tracey_parser *parser)
{
    size_t i;
    if (parser == NULL)
        return;
    for (i = 0; i < parser->error_count; i++)
        free(parser->errors[i].message);
    free(parser->errors);
    parser->errors = NULL;
    for (i = 0; i < parser->typedef_count; i++)
        free(parser->typedef_names[i]);
    free(parser->typedef_names);
    free(parser->typedef_types);
    for (i = 0; i < parser->typedef_count; i++)
        tracey_func_sig_free(parser->typedef_fn_sigs[i]);
    free(parser->typedef_fn_sigs);
    parser->typedef_names = NULL;
    parser->typedef_types = NULL;
    parser->typedef_fn_sigs = NULL;
    parser->typedef_count = 0;
    parser->typedef_cap = 0;

    parser->error_count = 0;
    parser->error_cap = 0;
    parser->had_error = false;
}


bool tracey_parser_check(const struct tracey_parser *parser, enum tracey_token_type type)
{
    return parser->has_current && parser->current.type == type;
}


bool tracey_parser_match(struct tracey_parser *parser, enum tracey_token_type type)
{
    if (tracey_parser_check(parser, type))
    {
        tracey_parser_advance(parser);
        return true;
    }
    return false;
}


bool tracey_parser_expect(struct tracey_parser *parser, enum tracey_token_type type,
                                 const char *msg)
{
    if (tracey_parser_check(parser, type))
    {
        tracey_parser_advance(parser);
        return true;
    }
    tracey_parser_error(parser, msg, &parser->current);
    return false;
}


void tracey_parser_synchronize(struct tracey_parser *parser)
{
    while (parser->has_current && parser->current.type != TRACEY_TOKEN_EOF)
    {
        if (parser->current.type == TRACEY_TOKEN_SEMICOLON)
        {
            tracey_parser_advance(parser);
            return;
        }
        if (parser->current.type == TRACEY_TOKEN_RBRACE)
            return;
        if (parser->current.type == TRACEY_TOKEN_KEYWORD_IF ||
            parser->current.type == TRACEY_TOKEN_KEYWORD_WHILE ||
            parser->current.type == TRACEY_TOKEN_KEYWORD_RETURN ||
            parser->current.type == TRACEY_TOKEN_KEYWORD_INT ||
            parser->current.type == TRACEY_TOKEN_KEYWORD_BOOL ||
            parser->current.type == TRACEY_TOKEN_KEYWORD_VOID ||
            parser->current.type == TRACEY_TOKEN_KEYWORD_CHAR ||
            parser->current.type == TRACEY_TOKEN_KEYWORD_FLOAT ||
            parser->current.type == TRACEY_TOKEN_KEYWORD_DOUBLE)
            return;
        tracey_parser_advance(parser);
    }
}


struct tracey_expr *tracey_make_binary(struct tracey_expr *left, enum tracey_binary_op op,
                                              struct tracey_expr *right)
{
    struct tracey_expr *e = tracey_expr_create(TRACEY_EXPR_BINARY, left ? left->loc : (struct tracey_source_loc){1,1});
    if (!e) return NULL;
    e->as.binary_expr.op = op;
    e->as.binary_expr.left = left;
    e->as.binary_expr.right = right;
    return e;
}


bool tracey_is_assignable(const struct tracey_expr *e)
{
    if (e == NULL) return false;
    return e->kind == TRACEY_EXPR_IDENT || e->kind == TRACEY_EXPR_INDEX ||
           e->kind == TRACEY_EXPR_DEREF || e->kind == TRACEY_EXPR_MEMBER;
}



enum tracey_type_kind tracey_int_literal_type(const struct tracey_token *tok)
{
    size_t i = 0;
    unsigned base = 10;
    size_t digits_start;
    char buf[16];
    size_t suf_len;
    size_t k;

    if (tok == NULL || tok->lexeme == NULL || tok->lexeme_len == 0)
        return TRACEY_TYPE_INT;

    if (tok->lexeme_len >= 2 && tok->lexeme[0] == '0' &&
        (tok->lexeme[1] == 'x' || tok->lexeme[1] == 'X'))
    {
        base = 16;
        i = 2;
    }
    else if (tok->lexeme_len >= 2 && tok->lexeme[0] == '0' &&
             (tok->lexeme[1] == 'b' || tok->lexeme[1] == 'B'))
    {
        base = 2;
        i = 2;
    }

    digits_start = i;

    if (base == 16)
    {
        while (i < tok->lexeme_len)
        {
            char c = tok->lexeme[i];
            if ((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'))
                i++;
            else
                break;
        }
    }
    else if (base == 2)
    {
        while (i < tok->lexeme_len && (tok->lexeme[i] == '0' || tok->lexeme[i] == '1'))
            i++;
    }
    else
    {
        while (i < tok->lexeme_len && tok->lexeme[i] >= '0' && tok->lexeme[i] <= '9')
            i++;
    }

    if (i == digits_start)
        return TRACEY_TYPE_INT;

    suf_len = tok->lexeme_len - i;
    if (suf_len == 0)
        return TRACEY_TYPE_INT;
    if (suf_len >= sizeof(buf))
        return TRACEY_TYPE_INT;

    for (k = 0; k < suf_len; k++)
    {
        char c = tok->lexeme[i + k];
        if (c >= 'A' && c <= 'Z')
            buf[k] = (char)(c - 'A' + 'a');
        else
            buf[k] = c;
    }
    buf[suf_len] = '\0';

    if (strcmp(buf, "u") == 0)
        return TRACEY_TYPE_UNSIGNED_INT;
    if (strcmp(buf, "l") == 0)
        return TRACEY_TYPE_LONG;
    if (strcmp(buf, "ul") == 0 || strcmp(buf, "lu") == 0)
        return TRACEY_TYPE_UNSIGNED_LONG;
    if (strcmp(buf, "ll") == 0)
        return TRACEY_TYPE_LONG_LONG;
    if (strcmp(buf, "ull") == 0 || strcmp(buf, "llu") == 0)
        return TRACEY_TYPE_UNSIGNED_LONG_LONG;

    return TRACEY_TYPE_INT;
}
