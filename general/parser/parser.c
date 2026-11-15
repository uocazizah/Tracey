#include "tracey/parser.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char *tracey_strdup_n(const char *s, size_t n)
{
    char *out = (char *)malloc(n + 1);
    if (out == NULL)
        return NULL;
    memcpy(out, s, n);
    out[n] = '\0';
    return out;
}

static char *tracey_token_strdup(const struct tracey_token *tok)
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
static bool tracey_parse_int_literal(const struct tracey_token *tok, unsigned long long *out)
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

    for (; i < tok->lexeme_len; i++)
    {
        char c = tok->lexeme[i];
        unsigned d;

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

    *out = v;
    return true;
}

static void tracey_parser_advance(struct tracey_parser *parser)
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

static void tracey_parser_error(struct tracey_parser *parser, const char *msg, const struct tracey_token *tok)
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
    parser->typedef_names = NULL;
    parser->typedef_types = NULL;
    parser->typedef_count = 0;
    parser->typedef_cap = 0;

    parser->error_count = 0;
    parser->error_cap = 0;
    parser->had_error = false;
}

static bool tracey_parser_check(const struct tracey_parser *parser, enum tracey_token_type type)
{
    return parser->has_current && parser->current.type == type;
}

static bool tracey_parser_match(struct tracey_parser *parser, enum tracey_token_type type)
{
    if (tracey_parser_check(parser, type))
    {
        tracey_parser_advance(parser);
        return true;
    }
    return false;
}

static bool tracey_parser_expect(struct tracey_parser *parser, enum tracey_token_type type,
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

static void tracey_parser_synchronize(struct tracey_parser *parser)
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

static bool tracey_parser_is_typedef_alias(const struct tracey_parser *parser)
{
    if (!parser->has_current || parser->current.type != TRACEY_TOKEN_IDENTIFIER)
        return false;
    for (size_t _ti = 0; _ti < parser->typedef_count; _ti++)
    {
        if (strlen(parser->typedef_names[_ti]) == parser->current.lexeme_len &&
            memcmp(parser->typedef_names[_ti], parser->current.lexeme, parser->current.lexeme_len) == 0)
            return true;
    }
    return false;
}

static enum tracey_type_kind tracey_parse_type_ex(struct tracey_parser *parser,
                                                  int *out_struct_id);

static enum tracey_type_kind tracey_parse_type(struct tracey_parser *parser)
{
    return tracey_parse_type_ex(parser, NULL);
}

/*
 * tracey_parse_type_ex - parse a type specifier.
 *
 * Returns the scalar type kind. When the type is a struct value type, the
 * function returns TRACEY_TYPE_STRUCT and, if out_struct_id is not NULL,
 * stores the index of the struct definition selected by the tag.
 * out_struct_id is set to -1 for every non-struct type. An unknown struct
 * tag is reported as an error and yields -1 as well.
 *
 * Pointers to struct types are not supported yet; a '*' after a struct tag
 * is reported as an error.
 */
static enum tracey_type_kind tracey_parse_type_ex(struct tracey_parser *parser,
                                                  int *out_struct_id)
{
    if (out_struct_id != NULL)
        *out_struct_id = -1;

    if (tracey_parser_check(parser, TRACEY_TOKEN_KEYWORD_STRUCT))
    {
        char *tag;
        int sid;

        tracey_parser_advance(parser); /* struct */
        if (!tracey_parser_check(parser, TRACEY_TOKEN_IDENTIFIER))
        {
            tracey_parser_error(parser, "Expected struct tag name", &parser->current);
            return TRACEY_TYPE_INT;
        }
        tag = tracey_token_strdup(&parser->current);
        tracey_parser_advance(parser);
        sid = tracey_program_find_struct_id(parser->program, tag);
        if (sid < 0)
        {
            char buf[256];
            snprintf(buf, sizeof(buf), "Unknown struct type '%s'", tag ? tag : "");
            tracey_parser_error(parser, buf, &parser->current);
        }
        free(tag);
        if (tracey_parser_check(parser, TRACEY_TOKEN_STAR))
        {
            tracey_parser_error(parser, "Pointers to struct types are not supported yet",
                                &parser->current);
            tracey_parser_advance(parser);
        }
        if (out_struct_id != NULL)
            *out_struct_id = sid;
        return TRACEY_TYPE_STRUCT;
    }

    /* Typedef alias. */
    if (tracey_parser_check(parser, TRACEY_TOKEN_IDENTIFIER))
    {
        for (size_t _ti = 0; _ti < parser->typedef_count; _ti++)
        {
            if (strlen(parser->typedef_names[_ti]) == parser->current.lexeme_len &&
                memcmp(parser->typedef_names[_ti], parser->current.lexeme, parser->current.lexeme_len) == 0)
            {
                enum tracey_type_kind _t = parser->typedef_types[_ti];
                tracey_parser_advance(parser);
                if (tracey_parser_check(parser, TRACEY_TOKEN_STAR))
                {
                    tracey_parser_advance(parser);
                    switch (_t)
                    {
                        case TRACEY_TYPE_INT: return TRACEY_TYPE_INT_PTR;
                        case TRACEY_TYPE_CHAR: return TRACEY_TYPE_CHAR_PTR;
                        case TRACEY_TYPE_FLOAT: return TRACEY_TYPE_FLOAT_PTR;
                        case TRACEY_TYPE_DOUBLE: return TRACEY_TYPE_DOUBLE_PTR;
                        default: return _t;
                    }
                }
                return _t;
            }
        }
    }

    bool is_unsigned = false;
    int long_count = 0;   /* 0 = none, 1 = long, 2 = long long */
    bool is_short = false;

    /* Parse optional sign/size qualifiers before the base type keyword. */
    for (;;)
    {
        if (tracey_parser_check(parser, TRACEY_TOKEN_KEYWORD_UNSIGNED))
        {
            is_unsigned = true;
            tracey_parser_advance(parser);
            continue;
        }
        if (tracey_parser_check(parser, TRACEY_TOKEN_KEYWORD_SIGNED))
        {
            tracey_parser_advance(parser);
            continue;
        }
        if (tracey_parser_check(parser, TRACEY_TOKEN_KEYWORD_LONG))
        {
            if (long_count < 2)
            {
                long_count++;
                tracey_parser_advance(parser);
                continue;
            }
            break;
        }
        if (tracey_parser_check(parser, TRACEY_TOKEN_KEYWORD_SHORT))
        {
            if (!is_short)
            {
                is_short = true;
                tracey_parser_advance(parser);
                continue;
            }
            break;
        }
        break;
    }

    /* Determine base type from the next keyword (defaults to int). */
    enum tracey_type_kind base = TRACEY_TYPE_INT;

    if (tracey_parser_check(parser, TRACEY_TOKEN_KEYWORD_INT))
    {
        tracey_parser_advance(parser);
        base = TRACEY_TYPE_INT;
    }
    else if (tracey_parser_check(parser, TRACEY_TOKEN_KEYWORD_CHAR))
    {
        tracey_parser_advance(parser);
        base = TRACEY_TYPE_CHAR;
    }
    else if (tracey_parser_check(parser, TRACEY_TOKEN_KEYWORD_FLOAT))
    {
        tracey_parser_advance(parser);
        base = TRACEY_TYPE_FLOAT;
    }
    else if (tracey_parser_check(parser, TRACEY_TOKEN_KEYWORD_DOUBLE))
    {
        tracey_parser_advance(parser);
        base = TRACEY_TYPE_DOUBLE;
    }
    else if (tracey_parser_check(parser, TRACEY_TOKEN_KEYWORD_BOOL))
    {
        tracey_parser_advance(parser);
        base = TRACEY_TYPE_BOOL;
    }
    else if (tracey_parser_check(parser, TRACEY_TOKEN_KEYWORD_VOID))
    {
        tracey_parser_advance(parser);
        base = TRACEY_TYPE_VOID;
    }
    else if (tracey_parser_check(parser, TRACEY_TOKEN_KEYWORD_SIZE_T))
    {
        tracey_parser_advance(parser);
        base = TRACEY_TYPE_SIZE_T;
    }

    /* Handle pointer suffix. */
    bool is_pointer = false;
    if (tracey_parser_check(parser, TRACEY_TOKEN_STAR))
    {
        tracey_parser_advance(parser);
        is_pointer = true;
    }

    /* Compute final scalar type. */
    if (!is_pointer)
    {
        if (long_count == 2)
            return is_unsigned ? TRACEY_TYPE_UNSIGNED_LONG_LONG : TRACEY_TYPE_LONG_LONG;
        if (long_count == 1)
            return is_unsigned ? TRACEY_TYPE_UNSIGNED_LONG : TRACEY_TYPE_LONG;
        if (is_short)
            return is_unsigned ? TRACEY_TYPE_UNSIGNED_SHORT : TRACEY_TYPE_SHORT;
        if (is_unsigned)
            return TRACEY_TYPE_UNSIGNED_INT;
        return base;
    }

    /* Compute final pointer type. */
    if (long_count == 2)
        return TRACEY_TYPE_LONG_LONG_PTR;
    if (long_count == 1)
        return TRACEY_TYPE_LONG_PTR;
    if (is_short)
        return TRACEY_TYPE_SHORT_PTR;
    switch (base)
    {
        case TRACEY_TYPE_INT:      return TRACEY_TYPE_INT_PTR;
        case TRACEY_TYPE_CHAR:     return TRACEY_TYPE_CHAR_PTR;
        case TRACEY_TYPE_FLOAT:    return TRACEY_TYPE_FLOAT_PTR;
        case TRACEY_TYPE_DOUBLE:   return TRACEY_TYPE_DOUBLE_PTR;
        case TRACEY_TYPE_SIZE_T:   return TRACEY_TYPE_SIZE_T_PTR;
        default:                   return TRACEY_TYPE_INT_PTR;
    }
}

static bool tracey_program_push_function(struct tracey_program *program, struct tracey_function_decl *fn)
{
    struct tracey_function_decl **new_items =
        (struct tracey_function_decl **)realloc(program->functions,
                                                (program->function_count + 1) * sizeof(*new_items));
    if (new_items == NULL)
        return false;
    new_items[program->function_count++] = fn;
    program->functions = new_items;
    return true;
}

static struct tracey_expr *tracey_parse_expression(struct tracey_parser *parser);
static struct tracey_stmt *tracey_parse_statement(struct tracey_parser *parser);
static struct tracey_stmt *tracey_parse_block(struct tracey_parser *parser);
static struct tracey_stmt *tracey_parse_for(struct tracey_parser *parser);
static struct tracey_stmt *tracey_parse_break(struct tracey_parser *parser);
static struct tracey_stmt *tracey_parse_continue(struct tracey_parser *parser);
static struct tracey_stmt *tracey_parse_goto(struct tracey_parser *parser);

/* Forward declarations for expression precedence */
static struct tracey_expr *tracey_parse_assignment(struct tracey_parser *parser);
static struct tracey_expr *tracey_parse_ternary(struct tracey_parser *parser);
static struct tracey_expr *tracey_parse_logical_or(struct tracey_parser *parser);
static struct tracey_expr *tracey_parse_logical_and(struct tracey_parser *parser);
static struct tracey_expr *tracey_parse_bitwise_or(struct tracey_parser *parser);
static struct tracey_expr *tracey_parse_bitwise_xor(struct tracey_parser *parser);
static struct tracey_expr *tracey_parse_bitwise_and(struct tracey_parser *parser);
static struct tracey_expr *tracey_parse_equality(struct tracey_parser *parser);
static struct tracey_expr *tracey_parse_relational(struct tracey_parser *parser);
static struct tracey_expr *tracey_parse_additive(struct tracey_parser *parser);
static struct tracey_expr *tracey_parse_multiplicative(struct tracey_parser *parser);
static struct tracey_expr *tracey_parse_unary(struct tracey_parser *parser);
static struct tracey_expr *tracey_parse_primary(struct tracey_parser *parser);

static struct tracey_expr *tracey_parse_expression(struct tracey_parser *parser)
{
    return tracey_parse_assignment(parser);
}

static struct tracey_expr *tracey_parse_primary(struct tracey_parser *parser)
{
    struct tracey_source_loc loc = {parser->current.line, parser->current.column};

    if (tracey_parser_check(parser, TRACEY_TOKEN_INTEGER))
    {
        struct tracey_token tok = parser->current;
        unsigned long long v = 0;
        tracey_parser_advance(parser);
        if (!tracey_parse_int_literal(&tok, &v))
            tracey_parser_error(parser, "Invalid integer literal", &tok);
        {
            struct tracey_expr *e = tracey_expr_create(TRACEY_EXPR_INT, (struct tracey_source_loc){tok.line, tok.column});
            if (e) e->as.int_expr.value = (long long)v;
            return e;
        }
    }
    if (tracey_parser_check(parser, TRACEY_TOKEN_FLOATING))
    {
        struct tracey_token tok = parser->current;
        char *tmp = tracey_token_strdup(&tok);
        bool is_float = false;
        size_t len;
        double v = 0.0;
        struct tracey_expr *e;
        tracey_parser_advance(parser);
        if (tmp == NULL)
            return NULL;
        len = strlen(tmp);
        if (len > 0 && (tmp[len - 1] == 'f' || tmp[len - 1] == 'F'))
        {
            is_float = true;
            tmp[len - 1] = '\0';
        }
        v = strtod(tmp, NULL);
        free(tmp);
        e = tracey_expr_create(TRACEY_EXPR_REAL, (struct tracey_source_loc){tok.line, tok.column});
        if (e)
        {
            e->as.real_expr.value = v;
            e->as.real_expr.is_float = is_float;
        }
        return e;
    }
    if (tracey_parser_check(parser, TRACEY_TOKEN_STRING))
    {
        struct tracey_token tok = parser->current;
        size_t i;
        size_t start = 0;
        size_t end = tok.lexeme_len;
        char *out;
        size_t out_len = 0;
        struct tracey_expr *e;
        tracey_parser_advance(parser);
        if (tok.lexeme_len >= 2 && tok.lexeme[0] == '"' && tok.lexeme[tok.lexeme_len - 1] == '"')
        {
            start = 1;
            end = tok.lexeme_len - 1;
        }
        out = (char *)malloc((end - start) + 1);
        if (out == NULL)
            return NULL;
        for (i = start; i < end; )
        {
            if (tok.lexeme[i] == '\\' && i + 1 < end)
            {
                char c = tok.lexeme[i + 1];
                if (c == 'n') { out[out_len++] = '\n'; i += 2; continue; }
                if (c == 't') { out[out_len++] = '\t'; i += 2; continue; }
                if (c == 'r') { out[out_len++] = '\r'; i += 2; continue; }
                if (c == '0') { out[out_len++] = '\0'; i += 2; continue; }
                if (c == '\\') { out[out_len++] = '\\'; i += 2; continue; }
                if (c == '"') { out[out_len++] = '"'; i += 2; continue; }
                out[out_len++] = tok.lexeme[i++];
            }
            else
            {
                out[out_len++] = tok.lexeme[i++];
            }
        }
        out[out_len] = '\0';
        e = tracey_expr_create(TRACEY_EXPR_STRING, (struct tracey_source_loc){tok.line, tok.column});
        if (e)
            e->as.string_expr.value = out;
        else
            free(out);
        return e;
    }
    if (tracey_parser_check(parser, TRACEY_TOKEN_KEYWORD_TRUE))
    {
        tracey_parser_advance(parser);
        {
            struct tracey_expr *e = tracey_expr_create(TRACEY_EXPR_BOOL, loc);
            if (e) e->as.bool_expr.value = true;
            return e;
        }
    }
    if (tracey_parser_check(parser, TRACEY_TOKEN_KEYWORD_FALSE))
    {
        tracey_parser_advance(parser);
        {
            struct tracey_expr *e = tracey_expr_create(TRACEY_EXPR_BOOL, loc);
            if (e) e->as.bool_expr.value = false;
            return e;
        }
    }
    if (tracey_parser_check(parser, TRACEY_TOKEN_CHAR))
    {
        struct tracey_token tok = parser->current;
        tracey_parser_advance(parser);
        {
            struct tracey_expr *e = tracey_expr_create(TRACEY_EXPR_CHAR, (struct tracey_source_loc){tok.line, tok.column});
            if (e)
            {
                /* Decode the char literal: lexeme is like 'a' or '\n'. */
                const char *lex = tok.lexeme;
                size_t len = tok.lexeme_len;
                int v = 0;
                if (len >= 3 && lex[0] == '\'' && lex[len - 1] == '\'')
                {
                    if (lex[1] == '\\' && len >= 4)
                    {
                        switch (lex[2])
                        {
                            case 'n': v = '\n'; break;
                            case 't': v = '\t'; break;
                            case 'r': v = '\r'; break;
                            case '0': v = '\0'; break;
                            case '\\': v = '\\'; break;
                            case '\'': v = '\''; break;
                            case '"': v = '"'; break;
                            default: v = (unsigned char)lex[2]; break;
                        }
                    }
                    else
                    {
                        v = (unsigned char)lex[1];
                    }
                }
                e->as.char_expr.value = v;
            }
            return e;
        }
    }
    if (tracey_parser_check(parser, TRACEY_TOKEN_IDENTIFIER))
    {
        struct tracey_token tok = parser->current;
        char *name = tracey_token_strdup(&tok);
        tracey_parser_advance(parser);
        if (tracey_parser_check(parser, TRACEY_TOKEN_LPAREN))
        {
            struct tracey_expr **args = NULL;
            size_t arg_count = 0;
            struct tracey_expr *call;
            tracey_parser_advance(parser);
            if (!tracey_parser_check(parser, TRACEY_TOKEN_RPAREN))
            {
                for (;;)
                {
                    struct tracey_expr *arg = tracey_parse_expression(parser);
                    struct tracey_expr **new_args;
                    if (arg == NULL)
                    {
                        free(name);
                        return NULL;
                    }
                    new_args = (struct tracey_expr **)realloc(args, (arg_count + 1) * sizeof(*new_args));
                    if (!new_args) { tracey_expr_destroy(arg); free(name); return NULL; }
                    args = new_args;
                    args[arg_count++] = arg;
                    if (!tracey_parser_match(parser, TRACEY_TOKEN_COMMA))
                        break;
                }
            }
            if (!tracey_parser_expect(parser, TRACEY_TOKEN_RPAREN, "expected ')'"))
            {
                size_t i;
                for (i = 0; i < arg_count; i++) tracey_expr_destroy(args[i]);
                free(args); free(name);
                return NULL;
            }
            call = tracey_expr_create(TRACEY_EXPR_CALL, (struct tracey_source_loc){tok.line, tok.column});
            if (!call) { free(name); return NULL; }
            call->as.call_expr.callee = name;
            call->as.call_expr.args = args;
            call->as.call_expr.arg_count = arg_count;
            return call;
        }
        {
            struct tracey_expr *e = tracey_expr_create(TRACEY_EXPR_IDENT, (struct tracey_source_loc){tok.line, tok.column});
            if (e) e->as.ident_expr.name = name; else free(name);
            return e;
        }
    }
    if (tracey_parser_check(parser, TRACEY_TOKEN_LPAREN))
    {
        struct tracey_expr *e;
        tracey_parser_advance(parser);
        e = tracey_parse_expression(parser);
        if (!e) return NULL;
        if (!tracey_parser_expect(parser, TRACEY_TOKEN_RPAREN, "expected ')'"))
        {
            tracey_expr_destroy(e);
            return NULL;
        }
        return e;
    }

    tracey_parser_error(parser, "Expected expression", &parser->current);
    return NULL;
}

static struct tracey_expr *tracey_parse_unary(struct tracey_parser *parser)
{
    if (tracey_parser_check(parser, TRACEY_TOKEN_KEYWORD_SIZEOF))
    {
        struct tracey_source_loc loc = {parser->current.line, parser->current.column};
        tracey_parser_advance(parser);
        if (tracey_parser_check(parser, TRACEY_TOKEN_LPAREN))
        {
            tracey_parser_advance(parser); /* consume '(' */
            if (tracey_parser_check(parser, TRACEY_TOKEN_KEYWORD_INT) ||
                tracey_parser_check(parser, TRACEY_TOKEN_KEYWORD_CHAR) ||
                tracey_parser_check(parser, TRACEY_TOKEN_KEYWORD_SHORT) ||
                tracey_parser_check(parser, TRACEY_TOKEN_KEYWORD_LONG) ||
                tracey_parser_check(parser, TRACEY_TOKEN_KEYWORD_FLOAT) ||
                tracey_parser_check(parser, TRACEY_TOKEN_KEYWORD_DOUBLE) ||
                tracey_parser_check(parser, TRACEY_TOKEN_KEYWORD_BOOL) ||
                tracey_parser_check(parser, TRACEY_TOKEN_KEYWORD_VOID) ||
                tracey_parser_check(parser, TRACEY_TOKEN_KEYWORD_UNSIGNED) ||
                tracey_parser_check(parser, TRACEY_TOKEN_KEYWORD_SIGNED) ||
                tracey_parser_check(parser, TRACEY_TOKEN_KEYWORD_SIZE_T))
            {
                (void)tracey_parse_type(parser);
            }
            else
            {
                struct tracey_expr *e = tracey_parse_expression(parser);
                tracey_expr_destroy(e);
            }
            tracey_parser_expect(parser, TRACEY_TOKEN_RPAREN, "expected ')'");
        }
        else
        {
            struct tracey_expr *e = tracey_parse_unary(parser);
            tracey_expr_destroy(e);
        }
        {
            struct tracey_expr *e = tracey_expr_create(TRACEY_EXPR_INT, loc);
            if (e) e->as.int_expr.value = 8;
            return e;
        }
    }
    if (tracey_parser_check(parser, TRACEY_TOKEN_PLUS_PLUS))
    {
        struct tracey_source_loc loc = {parser->current.line, parser->current.column};
        struct tracey_expr *opnd, *e;
        tracey_parser_advance(parser);
        opnd = tracey_parse_unary(parser);
        if (!opnd) return NULL;
        e = tracey_expr_create(TRACEY_EXPR_UNARY, loc);
        if (!e) { tracey_expr_destroy(opnd); return NULL; }
        e->as.unary_expr.op = TRACEY_UNARY_PRE_INC;
        e->as.unary_expr.operand = opnd;
        return e;
    }
    if (tracey_parser_check(parser, TRACEY_TOKEN_MINUS_MINUS))
    {
        struct tracey_source_loc loc = {parser->current.line, parser->current.column};
        struct tracey_expr *opnd, *e;
        tracey_parser_advance(parser);
        opnd = tracey_parse_unary(parser);
        if (!opnd) return NULL;
        e = tracey_expr_create(TRACEY_EXPR_UNARY, loc);
        if (!e) { tracey_expr_destroy(opnd); return NULL; }
        e->as.unary_expr.op = TRACEY_UNARY_PRE_DEC;
        e->as.unary_expr.operand = opnd;
        return e;
    }
    if (tracey_parser_check(parser, TRACEY_TOKEN_MINUS))
    {
        struct tracey_source_loc loc = {parser->current.line, parser->current.column};
        struct tracey_expr *opnd, *e;
        tracey_parser_advance(parser);
        opnd = tracey_parse_unary(parser);
        if (!opnd) return NULL;
        e = tracey_expr_create(TRACEY_EXPR_UNARY, loc);
        if (!e) { tracey_expr_destroy(opnd); return NULL; }
        e->as.unary_expr.op = TRACEY_UNARY_NEG;
        e->as.unary_expr.operand = opnd;
        return e;
    }
    if (tracey_parser_check(parser, TRACEY_TOKEN_BANG))
    {
        struct tracey_source_loc loc = {parser->current.line, parser->current.column};
        struct tracey_expr *opnd, *e;
        tracey_parser_advance(parser);
        opnd = tracey_parse_unary(parser);
        if (!opnd) return NULL;
        e = tracey_expr_create(TRACEY_EXPR_UNARY, loc);
        if (!e) { tracey_expr_destroy(opnd); return NULL; }
        e->as.unary_expr.op = TRACEY_UNARY_LOG_NOT;
        e->as.unary_expr.operand = opnd;
        return e;
    }
    if (tracey_parser_check(parser, TRACEY_TOKEN_TILDE))
    {
        struct tracey_source_loc loc = {parser->current.line, parser->current.column};
        struct tracey_expr *opnd, *e;
        tracey_parser_advance(parser);
        opnd = tracey_parse_unary(parser);
        if (!opnd) return NULL;
        e = tracey_expr_create(TRACEY_EXPR_UNARY, loc);
        if (!e) { tracey_expr_destroy(opnd); return NULL; }
        e->as.unary_expr.op = TRACEY_UNARY_BIT_NOT;
        e->as.unary_expr.operand = opnd;
        return e;
    }
    if (tracey_parser_check(parser, TRACEY_TOKEN_AMPERSAND))
    {
        struct tracey_source_loc loc = {parser->current.line, parser->current.column};
        struct tracey_expr *opnd, *e;
        tracey_parser_advance(parser);
        opnd = tracey_parse_unary(parser);
        if (!opnd) return NULL;
        e = tracey_expr_create(TRACEY_EXPR_ADDR, loc);
        if (!e) { tracey_expr_destroy(opnd); return NULL; }
        e->as.addr_expr.operand = opnd;
        return e;
    }
    if (tracey_parser_check(parser, TRACEY_TOKEN_STAR))
    {
        struct tracey_source_loc loc = {parser->current.line, parser->current.column};
        struct tracey_expr *opnd, *e;
        tracey_parser_advance(parser);
        opnd = tracey_parse_unary(parser);
        if (!opnd) return NULL;
        e = tracey_expr_create(TRACEY_EXPR_DEREF, loc);
        if (!e) { tracey_expr_destroy(opnd); return NULL; }
        e->as.deref_expr.operand = opnd;
        return e;
    }

    {
        struct tracey_expr *primary = tracey_parse_primary(parser);
        /*
         * Postfix loop: subscript and member access bind left to right at the
         * same precedence, so both are handled here before ++/--.
         */
        while (primary && (tracey_parser_check(parser, TRACEY_TOKEN_LBRACKET) ||
                           tracey_parser_check(parser, TRACEY_TOKEN_DOT)))
        {
            if (tracey_parser_check(parser, TRACEY_TOKEN_LBRACKET))
            {
                struct tracey_expr *idx, *node;
                tracey_parser_advance(parser);
                idx = tracey_parse_expression(parser);
                if (!idx) { tracey_expr_destroy(primary); return NULL; }
                if (!tracey_parser_expect(parser, TRACEY_TOKEN_RBRACKET, "expected ']'"))
                { tracey_expr_destroy(idx); tracey_expr_destroy(primary); return NULL; }
                node = tracey_expr_create(TRACEY_EXPR_INDEX, primary->loc);
                if (!node) { tracey_expr_destroy(idx); tracey_expr_destroy(primary); return NULL; }
                node->as.index_expr.base = primary;
                node->as.index_expr.index = idx;
                primary = node;
            }
            else
            {
                struct tracey_expr *node;
                tracey_parser_advance(parser); /* '.' */
                if (!tracey_parser_check(parser, TRACEY_TOKEN_IDENTIFIER))
                {
                    tracey_parser_error(parser, "Expected member name after '.'", &parser->current);
                    tracey_expr_destroy(primary);
                    return NULL;
                }
                node = tracey_expr_create(TRACEY_EXPR_MEMBER, primary->loc);
                if (!node) { tracey_expr_destroy(primary); return NULL; }
                node->as.member_expr.base = primary;
                node->as.member_expr.field = tracey_token_strdup(&parser->current);
                tracey_parser_advance(parser);
                primary = node;
            }
        }
        if (primary && tracey_parser_check(parser, TRACEY_TOKEN_PLUS_PLUS))
        {
            struct tracey_source_loc loc = primary->loc;
            struct tracey_expr *e = tracey_expr_create(TRACEY_EXPR_UNARY, loc);
            tracey_parser_advance(parser);
            if (!e) { tracey_expr_destroy(primary); return NULL; }
            e->as.unary_expr.op = TRACEY_UNARY_POST_INC;
            e->as.unary_expr.operand = primary;
            return e;
        }
        if (primary && tracey_parser_check(parser, TRACEY_TOKEN_MINUS_MINUS))
        {
            struct tracey_source_loc loc = primary->loc;
            struct tracey_expr *e = tracey_expr_create(TRACEY_EXPR_UNARY, loc);
            tracey_parser_advance(parser);
            if (!e) { tracey_expr_destroy(primary); return NULL; }
            e->as.unary_expr.op = TRACEY_UNARY_POST_DEC;
            e->as.unary_expr.operand = primary;
            return e;
        }
        return primary;
    }
}

static struct tracey_expr *tracey_make_binary(struct tracey_expr *left, enum tracey_binary_op op,
                                              struct tracey_expr *right)
{
    struct tracey_expr *e = tracey_expr_create(TRACEY_EXPR_BINARY, left ? left->loc : (struct tracey_source_loc){1,1});
    if (!e) return NULL;
    e->as.binary_expr.op = op;
    e->as.binary_expr.left = left;
    e->as.binary_expr.right = right;
    return e;
}

static struct tracey_expr *tracey_parse_multiplicative(struct tracey_parser *parser)
{
    struct tracey_expr *left = tracey_parse_unary(parser);
    if (!left) return NULL;
    while (tracey_parser_check(parser, TRACEY_TOKEN_STAR) ||
           tracey_parser_check(parser, TRACEY_TOKEN_SLASH) ||
           tracey_parser_check(parser, TRACEY_TOKEN_PERCENT))
    {
        enum tracey_binary_op op;
        struct tracey_expr *right;
        if (tracey_parser_check(parser, TRACEY_TOKEN_STAR)) op = TRACEY_BINARY_MUL;
        else if (tracey_parser_check(parser, TRACEY_TOKEN_SLASH)) op = TRACEY_BINARY_DIV;
        else op = TRACEY_BINARY_MOD;
        tracey_parser_advance(parser);
        right = tracey_parse_unary(parser);
        if (!right) { tracey_expr_destroy(left); return NULL; }
        left = tracey_make_binary(left, op, right);
        if (!left) return NULL;
    }
    return left;
}

static struct tracey_expr *tracey_parse_additive(struct tracey_parser *parser)
{
    struct tracey_expr *left = tracey_parse_multiplicative(parser);
    if (!left) return NULL;
    while (tracey_parser_check(parser, TRACEY_TOKEN_PLUS) ||
           tracey_parser_check(parser, TRACEY_TOKEN_MINUS))
    {
        enum tracey_binary_op op = tracey_parser_check(parser, TRACEY_TOKEN_PLUS) ? TRACEY_BINARY_ADD : TRACEY_BINARY_SUB;
        struct tracey_expr *right;
        tracey_parser_advance(parser);
        right = tracey_parse_multiplicative(parser);
        if (!right) { tracey_expr_destroy(left); return NULL; }
        left = tracey_make_binary(left, op, right);
        if (!left) return NULL;
    }
    return left;
}

static struct tracey_expr *tracey_parse_shift(struct tracey_parser *parser)
{
    struct tracey_expr *left = tracey_parse_additive(parser);
    if (!left) return NULL;
    while (tracey_parser_check(parser, TRACEY_TOKEN_LESS_LESS) ||
           tracey_parser_check(parser, TRACEY_TOKEN_GREATER_GREATER))
    {
        enum tracey_binary_op op = tracey_parser_check(parser, TRACEY_TOKEN_LESS_LESS)
                                       ? TRACEY_BINARY_SHIFT_LEFT
                                       : TRACEY_BINARY_SHIFT_RIGHT;
        struct tracey_expr *right;
        tracey_parser_advance(parser);
        right = tracey_parse_additive(parser);
        if (!right) { tracey_expr_destroy(left); return NULL; }
        left = tracey_make_binary(left, op, right);
        if (!left) return NULL;
    }
    return left;
}

static struct tracey_expr *tracey_parse_relational(struct tracey_parser *parser)
{
    struct tracey_expr *left = tracey_parse_shift(parser);
    if (!left) return NULL;
    while (tracey_parser_check(parser, TRACEY_TOKEN_LESS) ||
           tracey_parser_check(parser, TRACEY_TOKEN_GREATER) ||
           tracey_parser_check(parser, TRACEY_TOKEN_LESS_EQUAL) ||
           tracey_parser_check(parser, TRACEY_TOKEN_GREATER_EQUAL))
    {
        enum tracey_binary_op op;
        struct tracey_expr *right;
        if (tracey_parser_check(parser, TRACEY_TOKEN_LESS)) op = TRACEY_BINARY_LT;
        else if (tracey_parser_check(parser, TRACEY_TOKEN_GREATER)) op = TRACEY_BINARY_GT;
        else if (tracey_parser_check(parser, TRACEY_TOKEN_LESS_EQUAL)) op = TRACEY_BINARY_LE;
        else op = TRACEY_BINARY_GE;
        tracey_parser_advance(parser);
        right = tracey_parse_shift(parser);
        if (!right) { tracey_expr_destroy(left); return NULL; }
        left = tracey_make_binary(left, op, right);
        if (!left) return NULL;
    }
    return left;
}

static struct tracey_expr *tracey_parse_equality(struct tracey_parser *parser)
{
    struct tracey_expr *left = tracey_parse_relational(parser);
    if (!left) return NULL;
    while (tracey_parser_check(parser, TRACEY_TOKEN_EQUAL_EQUAL) ||
           tracey_parser_check(parser, TRACEY_TOKEN_BANG_EQUAL))
    {
        enum tracey_binary_op op = tracey_parser_check(parser, TRACEY_TOKEN_EQUAL_EQUAL) ? TRACEY_BINARY_EQ : TRACEY_BINARY_NE;
        struct tracey_expr *right;
        tracey_parser_advance(parser);
        right = tracey_parse_relational(parser);
        if (!right) { tracey_expr_destroy(left); return NULL; }
        left = tracey_make_binary(left, op, right);
        if (!left) return NULL;
    }
    return left;
}

static struct tracey_expr *tracey_parse_logical_and(struct tracey_parser *parser)
{
    struct tracey_expr *left = tracey_parse_bitwise_or(parser);
    if (!left) return NULL;
    while (tracey_parser_check(parser, TRACEY_TOKEN_AMP_AMP))
    {
        struct tracey_expr *right;
        tracey_parser_advance(parser);
        right = tracey_parse_bitwise_or(parser);
        if (!right) { tracey_expr_destroy(left); return NULL; }
        left = tracey_make_binary(left, TRACEY_BINARY_LOG_AND, right);
        if (!left) return NULL;
    }
    return left;
}

static struct tracey_expr *tracey_parse_bitwise_or(struct tracey_parser *parser)
{
    struct tracey_expr *left = tracey_parse_bitwise_xor(parser);
    if (!left) return NULL;
    while (tracey_parser_check(parser, TRACEY_TOKEN_PIPE))
    {
        struct tracey_expr *right;
        tracey_parser_advance(parser);
        right = tracey_parse_bitwise_xor(parser);
        if (!right) { tracey_expr_destroy(left); return NULL; }
        left = tracey_make_binary(left, TRACEY_BINARY_BIT_OR, right);
        if (!left) return NULL;
    }
    return left;
}

static struct tracey_expr *tracey_parse_bitwise_xor(struct tracey_parser *parser)
{
    struct tracey_expr *left = tracey_parse_bitwise_and(parser);
    if (!left) return NULL;
    while (tracey_parser_check(parser, TRACEY_TOKEN_CARET))
    {
        struct tracey_expr *right;
        tracey_parser_advance(parser);
        right = tracey_parse_bitwise_and(parser);
        if (!right) { tracey_expr_destroy(left); return NULL; }
        left = tracey_make_binary(left, TRACEY_BINARY_BIT_XOR, right);
        if (!left) return NULL;
    }
    return left;
}

static struct tracey_expr *tracey_parse_bitwise_and(struct tracey_parser *parser)
{
    struct tracey_expr *left = tracey_parse_equality(parser);
    if (!left) return NULL;
    while (tracey_parser_check(parser, TRACEY_TOKEN_AMPERSAND))
    {
        struct tracey_expr *right;
        tracey_parser_advance(parser);
        right = tracey_parse_equality(parser);
        if (!right) { tracey_expr_destroy(left); return NULL; }
        left = tracey_make_binary(left, TRACEY_BINARY_BIT_AND, right);
        if (!left) return NULL;
    }
    return left;
}

static struct tracey_expr *tracey_parse_logical_or(struct tracey_parser *parser)
{
    struct tracey_expr *left = tracey_parse_logical_and(parser);
    if (!left) return NULL;
    while (tracey_parser_check(parser, TRACEY_TOKEN_PIPE_PIPE))
    {
        struct tracey_expr *right;
        tracey_parser_advance(parser);
        right = tracey_parse_logical_and(parser);
        if (!right) { tracey_expr_destroy(left); return NULL; }
        left = tracey_make_binary(left, TRACEY_BINARY_LOG_OR, right);
        if (!left) return NULL;
    }
    return left;
}

static bool tracey_is_assignable(const struct tracey_expr *e)
{
    if (e == NULL) return false;
    return e->kind == TRACEY_EXPR_IDENT || e->kind == TRACEY_EXPR_INDEX ||
           e->kind == TRACEY_EXPR_DEREF || e->kind == TRACEY_EXPR_MEMBER;
}

static struct tracey_expr *tracey_parse_assignment(struct tracey_parser *parser)
{
    struct tracey_expr *left = tracey_parse_ternary(parser);
    if (!left) return NULL;
    if (tracey_parser_check(parser, TRACEY_TOKEN_ASSIGN) && tracey_is_assignable(left))
    {
        struct tracey_expr *right;
        tracey_parser_advance(parser);
        right = tracey_parse_assignment(parser);
        if (!right) { tracey_expr_destroy(left); return NULL; }
        left = tracey_make_binary(left, TRACEY_BINARY_ASSIGN, right);
    }
    else if (tracey_parser_check(parser, TRACEY_TOKEN_PLUS_EQUAL) &&
             tracey_is_assignable(left))
    {
        struct tracey_expr *right;
        tracey_parser_advance(parser);
        right = tracey_parse_assignment(parser);
        if (!right) { tracey_expr_destroy(left); return NULL; }
        left = tracey_make_binary(left, TRACEY_BINARY_ADD_ASSIGN, right);
    }
    else if (tracey_parser_check(parser, TRACEY_TOKEN_MINUS_EQUAL) &&
             tracey_is_assignable(left))
    {
        struct tracey_expr *right;
        tracey_parser_advance(parser);
        right = tracey_parse_assignment(parser);
        if (!right) { tracey_expr_destroy(left); return NULL; }
        left = tracey_make_binary(left, TRACEY_BINARY_SUB_ASSIGN, right);
    }
    else if (tracey_parser_check(parser, TRACEY_TOKEN_STAR_EQUAL) &&
             tracey_is_assignable(left))
    {
        struct tracey_expr *right;
        tracey_parser_advance(parser);
        right = tracey_parse_assignment(parser);
        if (!right) { tracey_expr_destroy(left); return NULL; }
        left = tracey_make_binary(left, TRACEY_BINARY_MUL_ASSIGN, right);
    }
    else if (tracey_parser_check(parser, TRACEY_TOKEN_SLASH_EQUAL) &&
             tracey_is_assignable(left))
    {
        struct tracey_expr *right;
        tracey_parser_advance(parser);
        right = tracey_parse_assignment(parser);
        if (!right) { tracey_expr_destroy(left); return NULL; }
        left = tracey_make_binary(left, TRACEY_BINARY_DIV_ASSIGN, right);
    }
    else if (tracey_parser_check(parser, TRACEY_TOKEN_PERCENT_EQUAL) &&
             tracey_is_assignable(left))
    {
        struct tracey_expr *right;
        tracey_parser_advance(parser);
        right = tracey_parse_assignment(parser);
        if (!right) { tracey_expr_destroy(left); return NULL; }
        left = tracey_make_binary(left, TRACEY_BINARY_MOD_ASSIGN, right);
    }
    else if (tracey_parser_check(parser, TRACEY_TOKEN_LESS_LESS_EQUAL) &&
             tracey_is_assignable(left))
    {
        struct tracey_expr *right;
        tracey_parser_advance(parser);
        right = tracey_parse_assignment(parser);
        if (!right) { tracey_expr_destroy(left); return NULL; }
        left = tracey_make_binary(left, TRACEY_BINARY_SHL_ASSIGN, right);
    }
    else if (tracey_parser_check(parser, TRACEY_TOKEN_GREATER_GREATER_EQUAL) &&
             tracey_is_assignable(left))
    {
        struct tracey_expr *right;
        tracey_parser_advance(parser);
        right = tracey_parse_assignment(parser);
        if (!right) { tracey_expr_destroy(left); return NULL; }
        left = tracey_make_binary(left, TRACEY_BINARY_SHR_ASSIGN, right);
    }
    else if (tracey_parser_check(parser, TRACEY_TOKEN_AMPERSAND_EQUAL) &&
             tracey_is_assignable(left))
    {
        struct tracey_expr *right;
        tracey_parser_advance(parser);
        right = tracey_parse_assignment(parser);
        if (!right) { tracey_expr_destroy(left); return NULL; }
        left = tracey_make_binary(left, TRACEY_BINARY_AND_ASSIGN, right);
    }
    else if (tracey_parser_check(parser, TRACEY_TOKEN_PIPE_EQUAL) &&
             tracey_is_assignable(left))
    {
        struct tracey_expr *right;
        tracey_parser_advance(parser);
        right = tracey_parse_assignment(parser);
        if (!right) { tracey_expr_destroy(left); return NULL; }
        left = tracey_make_binary(left, TRACEY_BINARY_OR_ASSIGN, right);
    }
    else if (tracey_parser_check(parser, TRACEY_TOKEN_CARET_EQUAL) &&
             tracey_is_assignable(left))
    {
        struct tracey_expr *right;
        tracey_parser_advance(parser);
        right = tracey_parse_assignment(parser);
        if (!right) { tracey_expr_destroy(left); return NULL; }
        left = tracey_make_binary(left, TRACEY_BINARY_XOR_ASSIGN, right);
    }
    return left;
}

static struct tracey_expr *tracey_parse_ternary(struct tracey_parser *parser)
{
    struct tracey_expr *cond = tracey_parse_logical_or(parser);
    if (!cond) return NULL;
    if (tracey_parser_match(parser, TRACEY_TOKEN_QUESTION))
    {
        struct tracey_expr *then_branch = tracey_parse_expression(parser);
        if (!then_branch)
        {
            tracey_expr_destroy(cond);
            return NULL;
        }
        if (!tracey_parser_expect(parser, TRACEY_TOKEN_COLON, "expected ':'"))
        {
            tracey_expr_destroy(cond);
            tracey_expr_destroy(then_branch);
            return NULL;
        }
        struct tracey_expr *else_branch = tracey_parse_expression(parser);
        if (!else_branch)
        {
            tracey_expr_destroy(cond);
            tracey_expr_destroy(then_branch);
            return NULL;
        }
        {
            struct tracey_expr *e = tracey_expr_create(TRACEY_EXPR_TERNARY, cond->loc);
            if (!e)
            {
                tracey_expr_destroy(cond);
                tracey_expr_destroy(then_branch);
                tracey_expr_destroy(else_branch);
                return NULL;
            }
            e->as.ternary_expr.condition = cond;
            e->as.ternary_expr.then_branch = then_branch;
            e->as.ternary_expr.else_branch = else_branch;
            return e;
        }
    }
    return cond;
}

static bool tracey_block_push(struct tracey_block_stmt *block, struct tracey_stmt *stmt)
{
    struct tracey_stmt **new_items = (struct tracey_stmt **)realloc(block->statements,
                                                                    (block->count + 1) * sizeof(*new_items));
    if (!new_items) return false;
    new_items[block->count++] = stmt;
    block->statements = new_items;
    return true;
}

static struct tracey_stmt *tracey_parse_block(struct tracey_parser *parser)
{
    struct tracey_source_loc loc = {parser->current.line, parser->current.column};
    struct tracey_stmt *block;
    if (!tracey_parser_expect(parser, TRACEY_TOKEN_LBRACE, "expected '{'"))
        return NULL;
    block = tracey_stmt_create(TRACEY_STMT_BLOCK, loc);
    if (!block) return NULL;
    while (parser->has_current && parser->current.type != TRACEY_TOKEN_RBRACE &&
           parser->current.type != TRACEY_TOKEN_EOF)
    {
        struct tracey_stmt *s = tracey_parse_statement(parser);
        if (s)
        {
            if (!tracey_block_push(&block->as.block_stmt, s))
            { tracey_stmt_destroy(s); tracey_stmt_destroy(block); return NULL; }
        }
        else
        {
            tracey_parser_synchronize(parser);
            if (tracey_parser_check(parser, TRACEY_TOKEN_SEMICOLON))
                tracey_parser_advance(parser);
        }
    }
    tracey_parser_expect(parser, TRACEY_TOKEN_RBRACE, "expected '}'");
    return block;
}

static struct tracey_stmt *tracey_parse_if(struct tracey_parser *parser)
{
    struct tracey_source_loc loc = {parser->current.line, parser->current.column};
    struct tracey_stmt *s = tracey_stmt_create(TRACEY_STMT_IF, loc);
    if (!s) return NULL;
    tracey_parser_advance(parser);
    if (!tracey_parser_expect(parser, TRACEY_TOKEN_LPAREN, "expected '('")) goto fail;
    s->as.if_stmt.condition = tracey_parse_expression(parser);
    if (!s->as.if_stmt.condition) goto fail;
    if (!tracey_parser_expect(parser, TRACEY_TOKEN_RPAREN, "expected ')'")) goto fail;
    s->as.if_stmt.then_branch = tracey_parse_statement(parser);
    if (!s->as.if_stmt.then_branch) goto fail;
    if (tracey_parser_match(parser, TRACEY_TOKEN_KEYWORD_ELSE))
    {
        s->as.if_stmt.else_branch = tracey_parse_statement(parser);
        if (!s->as.if_stmt.else_branch) goto fail;
    }
    return s;
fail:
    tracey_stmt_destroy(s);
    return NULL;
}

static struct tracey_stmt *tracey_parse_while(struct tracey_parser *parser)
{
    struct tracey_source_loc loc = {parser->current.line, parser->current.column};
    struct tracey_stmt *s = tracey_stmt_create(TRACEY_STMT_WHILE, loc);
    if (!s) return NULL;
    tracey_parser_advance(parser);
    if (!tracey_parser_expect(parser, TRACEY_TOKEN_LPAREN, "expected '('")) goto fail;
    s->as.while_stmt.condition = tracey_parse_expression(parser);
    if (!s->as.while_stmt.condition) goto fail;
    if (!tracey_parser_expect(parser, TRACEY_TOKEN_RPAREN, "expected ')'")) goto fail;
    s->as.while_stmt.body = tracey_parse_statement(parser);
    if (!s->as.while_stmt.body) goto fail;
    return s;
fail:
    tracey_stmt_destroy(s);
    return NULL;
}

static struct tracey_stmt *tracey_parse_do(struct tracey_parser *parser)
{
    struct tracey_source_loc loc = {parser->current.line, parser->current.column};
    struct tracey_stmt *s = tracey_stmt_create(TRACEY_STMT_DO_WHILE, loc);
    if (!s) return NULL;
    tracey_parser_advance(parser);
    s->as.do_while_stmt.body = tracey_parse_statement(parser);
    if (!s->as.do_while_stmt.body) goto fail;
    if (!tracey_parser_expect(parser, TRACEY_TOKEN_KEYWORD_WHILE, "expected 'while'")) goto fail;
    if (!tracey_parser_expect(parser, TRACEY_TOKEN_LPAREN, "expected '('")) goto fail;
    s->as.do_while_stmt.condition = tracey_parse_expression(parser);
    if (!s->as.do_while_stmt.condition) goto fail;
    if (!tracey_parser_expect(parser, TRACEY_TOKEN_RPAREN, "expected ')'")) goto fail;
    if (!tracey_parser_expect(parser, TRACEY_TOKEN_SEMICOLON, "expected ';'")) goto fail;
    return s;
fail:
    tracey_stmt_destroy(s);
    return NULL;
}

static struct tracey_stmt *tracey_parse_switch(struct tracey_parser *parser)
{
    struct tracey_source_loc loc = {parser->current.line, parser->current.column};
    struct tracey_stmt *sw;
    tracey_parser_advance(parser); /* switch */
    if (!tracey_parser_expect(parser, TRACEY_TOKEN_LPAREN, "expected '('")) return NULL;
    sw = tracey_stmt_create(TRACEY_STMT_SWITCH, loc);
    if (!sw) return NULL;
    sw->as.switch_stmt.condition = tracey_parse_expression(parser);
    if (!sw->as.switch_stmt.condition) { tracey_stmt_destroy(sw); return NULL; }
    if (!tracey_parser_expect(parser, TRACEY_TOKEN_RPAREN, "expected ')'")) { tracey_stmt_destroy(sw); return NULL; }
    if (!tracey_parser_expect(parser, TRACEY_TOKEN_LBRACE, "expected '{'")) { tracey_stmt_destroy(sw); return NULL; }

    while (parser->has_current && parser->current.type != TRACEY_TOKEN_RBRACE &&
           parser->current.type != TRACEY_TOKEN_EOF)
    {
        if (tracey_parser_check(parser, TRACEY_TOKEN_KEYWORD_CASE))
        {
            tracey_parser_advance(parser);
            if (tracey_parser_check(parser, TRACEY_TOKEN_INTEGER))
            {
                unsigned long long cv = 0;
                struct tracey_token cvtok = parser->current;
                if (!tracey_parse_int_literal(&cvtok, &cv))
                    tracey_parser_error(parser, "Invalid case label", &cvtok);
                tracey_parser_advance(parser);
                if (tracey_parser_check(parser, TRACEY_TOKEN_COLON))
                    tracey_parser_advance(parser);
                if (sw->as.switch_stmt.case_count == sw->as.switch_stmt.case_cap)
                {
                    size_t cap = sw->as.switch_stmt.case_cap ? sw->as.switch_stmt.case_cap * 2 : 8;
                    struct tracey_switch_case *nc = (struct tracey_switch_case *)realloc(sw->as.switch_stmt.cases, cap * sizeof(*nc));
                    if (!nc) { tracey_stmt_destroy(sw); return NULL; }
                    sw->as.switch_stmt.cases = nc;
                    sw->as.switch_stmt.case_cap = cap;
                }
                sw->as.switch_stmt.cases[sw->as.switch_stmt.case_count].value = (long)cv;
                sw->as.switch_stmt.cases[sw->as.switch_stmt.case_count].has_value = true;
                /* parse statements until next case/default/} */
                {
                    struct tracey_stmt *blk = tracey_stmt_create(TRACEY_STMT_BLOCK, loc);
                    if (!blk) { tracey_stmt_destroy(sw); return NULL; }
                    while (parser->has_current && parser->current.type != TRACEY_TOKEN_RBRACE &&
                           parser->current.type != TRACEY_TOKEN_KEYWORD_CASE &&
                           parser->current.type != TRACEY_TOKEN_KEYWORD_DEFAULT &&
                           parser->current.type != TRACEY_TOKEN_EOF)
                    {
                        struct tracey_stmt *st = tracey_parse_statement(parser);
                        if (st)
                        {
                            if (!tracey_block_push(&blk->as.block_stmt, st)) { tracey_stmt_destroy(st); tracey_stmt_destroy(blk); tracey_stmt_destroy(sw); return NULL; }
                        }
                        else
                        {
                            tracey_parser_synchronize(parser);
                            if (tracey_parser_check(parser, TRACEY_TOKEN_SEMICOLON))
                                tracey_parser_advance(parser);
                        }
                    }
                    sw->as.switch_stmt.cases[sw->as.switch_stmt.case_count].body = blk;
                    sw->as.switch_stmt.case_count++;
                }
            }
            else
            {
                tracey_stmt_destroy(sw);
                return NULL;
            }
        }
        else if (tracey_parser_check(parser, TRACEY_TOKEN_KEYWORD_DEFAULT))
        {
            tracey_parser_advance(parser);
            if (tracey_parser_check(parser, TRACEY_TOKEN_COLON))
                tracey_parser_advance(parser);
            {
                struct tracey_stmt *blk = tracey_stmt_create(TRACEY_STMT_BLOCK, loc);
                if (!blk) { tracey_stmt_destroy(sw); return NULL; }
                while (parser->has_current && parser->current.type != TRACEY_TOKEN_RBRACE &&
                       parser->current.type != TRACEY_TOKEN_KEYWORD_CASE &&
                       parser->current.type != TRACEY_TOKEN_EOF)
                {
                    struct tracey_stmt *st = tracey_parse_statement(parser);
                    if (st)
                    {
                        if (!tracey_block_push(&blk->as.block_stmt, st)) { tracey_stmt_destroy(st); tracey_stmt_destroy(blk); tracey_stmt_destroy(sw); return NULL; }
                    }
                    else
                    {
                        tracey_parser_synchronize(parser);
                        if (tracey_parser_check(parser, TRACEY_TOKEN_SEMICOLON))
                            tracey_parser_advance(parser);
                    }
                }
                sw->as.switch_stmt.default_body = blk;
            }
        }
        else
        {
            break;
        }
    }
    tracey_parser_expect(parser, TRACEY_TOKEN_RBRACE, "expected '}'");
    return sw;
}

/*
 * tracey_parse_struct_definition - parse a file-scope struct declaration.
 *
 * The current token must be the struct keyword. The function registers the
 * tag with parser->program, parses the brace-enclosed member list, and
 * consumes the trailing semicolon. Members are limited to scalar and struct
 * value types; a member may not be an array or a pointer.
 *
 * Returns true when the definition and its terminating semicolon were
 * consumed, or false after reporting an error. On failure the caller
 * synchronizes at the statement level; a partially built definition may
 * remain registered in the program.
 */
static bool tracey_parse_struct_definition(struct tracey_parser *parser)
{
    char *tag;
    int sid;

    tracey_parser_advance(parser); /* struct */
    if (!tracey_parser_check(parser, TRACEY_TOKEN_IDENTIFIER))
    {
        tracey_parser_error(parser, "Expected struct tag name", &parser->current);
        return false;
    }
    tag = tracey_token_strdup(&parser->current);
    tracey_parser_advance(parser);

    if (!tracey_parser_expect(parser, TRACEY_TOKEN_LBRACE, "expected '{'"))
    {
        free(tag);
        return false;
    }

    sid = tracey_program_add_struct(parser->program, tag);
    free(tag);
    if (sid < 0)
        return false;

    while (parser->has_current && parser->current.type != TRACEY_TOKEN_RBRACE &&
           parser->current.type != TRACEY_TOKEN_EOF)
    {
        int member_sid = -1;
        enum tracey_type_kind mt;

        if (tracey_parser_check(parser, TRACEY_TOKEN_SEMICOLON))
        {
            tracey_parser_advance(parser);
            continue;
        }

        mt = tracey_parse_type_ex(parser, &member_sid);
        if (!tracey_parser_check(parser, TRACEY_TOKEN_IDENTIFIER))
        {
            tracey_parser_error(parser, "Expected struct member name", &parser->current);
            tracey_parser_synchronize(parser);
            continue;
        }
        {
            struct tracey_token name_tok = parser->current;
            char *mname = tracey_token_strdup(&name_tok);
            size_t msize = 8;

            if (mt == TRACEY_TYPE_STRUCT && member_sid >= 0 &&
                (size_t)member_sid < parser->program->struct_count)
                msize = parser->program->structs[member_sid].size;

            if (!tracey_struct_add_member(&parser->program->structs[sid], mname, mt,
                                          member_sid, msize))
            {
                char buf[256];
                snprintf(buf, sizeof(buf), "Duplicate struct member '%s'", mname ? mname : "");
                tracey_parser_error(parser, buf, &name_tok);
            }
            free(mname);
        }
        tracey_parser_advance(parser);
        if (!tracey_parser_expect(parser, TRACEY_TOKEN_SEMICOLON, "expected ';' after member"))
            tracey_parser_synchronize(parser);
    }

    if (!tracey_parser_expect(parser, TRACEY_TOKEN_RBRACE, "expected '}'"))
        return false;
    if (!tracey_parser_expect(parser, TRACEY_TOKEN_SEMICOLON, "expected ';' after struct definition"))
        return false;
    return true;
}

static bool tracey_parser_add_typedef(struct tracey_parser *parser, const char *name,
                                         enum tracey_type_kind type)
{
    if (parser->typedef_count == parser->typedef_cap)
    {
        size_t cap = parser->typedef_cap ? parser->typedef_cap * 2 : 8;
        char **nn = (char **)realloc(parser->typedef_names, cap * sizeof(*nn));
        enum tracey_type_kind *tt = (enum tracey_type_kind *)realloc(parser->typedef_types, cap * sizeof(*tt));
        if (!nn || !tt) return false;
        parser->typedef_names = nn;
        parser->typedef_types = tt;
        parser->typedef_cap = cap;
    }
    parser->typedef_names[parser->typedef_count] = strdup(name);
    parser->typedef_types[parser->typedef_count] = type;
    parser->typedef_count++;
    return true;
}

static bool tracey_parse_typedef(struct tracey_parser *parser)
{
    enum tracey_type_kind t;
    struct tracey_token name_tok;
    tracey_parser_advance(parser); /* typedef */
    t = tracey_parse_type(parser);
    if (!tracey_parser_check(parser, TRACEY_TOKEN_IDENTIFIER))
    {
        tracey_parser_error(parser, "Expected typedef name", &parser->current);
        return false;
    }
    name_tok = parser->current;
    {
        char *nm = tracey_token_strdup(&name_tok);
        tracey_parser_add_typedef(parser, nm, t);
        free(nm);
    }
    tracey_parser_advance(parser);
    if (!tracey_parser_expect(parser, TRACEY_TOKEN_SEMICOLON, "expected ';'"))
        return false;
    return true;
}

static struct tracey_stmt *tracey_parse_var_decl_as_stmt(struct tracey_parser *parser)
{
    int struct_id = -1;
    enum tracey_type_kind t = tracey_parse_type_ex(parser, &struct_id);
    struct tracey_token name_tok;
    struct tracey_stmt *vd;

    if (!tracey_parser_check(parser, TRACEY_TOKEN_IDENTIFIER))
    {
        tracey_parser_error(parser, "Expected variable name", &parser->current);
        return NULL;
    }
    name_tok = parser->current;
    vd = tracey_stmt_create(TRACEY_STMT_VAR_DECL,
        (struct tracey_source_loc){name_tok.line, name_tok.column});
    if (!vd) return NULL;
    vd->as.var_decl_stmt.type = t;
    vd->as.var_decl_stmt.struct_id = struct_id;
    vd->as.var_decl_stmt.name = tracey_token_strdup(&name_tok);
    tracey_parser_advance(parser);
    if (tracey_parser_check(parser, TRACEY_TOKEN_LBRACKET))
    {
        tracey_parser_advance(parser);
        if (tracey_parser_check(parser, TRACEY_TOKEN_INTEGER))
        {
            unsigned long long sv = 0;
            struct tracey_token sztok = parser->current;
            if (!tracey_parse_int_literal(&sztok, &sv))
                tracey_parser_error(parser, "Invalid array size", &sztok);
            vd->as.var_decl_stmt.array_size = (size_t)sv;
            tracey_parser_advance(parser);
        }
        if (!tracey_parser_expect(parser, TRACEY_TOKEN_RBRACKET, "expected ']'"))
        { tracey_stmt_destroy(vd); return NULL; }
    }
    if (tracey_parser_match(parser, TRACEY_TOKEN_ASSIGN))
    {
        vd->as.var_decl_stmt.init = tracey_parse_expression(parser);
        if (!vd->as.var_decl_stmt.init) { tracey_stmt_destroy(vd); return NULL; }
    }
    return vd;
}

static struct tracey_stmt *tracey_parse_for(struct tracey_parser *parser)
{
    struct tracey_source_loc loc = {parser->current.line, parser->current.column};
    struct tracey_stmt *s = tracey_stmt_create(TRACEY_STMT_FOR, loc);
    if (!s) return NULL;
    tracey_parser_advance(parser);
    if (!tracey_parser_expect(parser, TRACEY_TOKEN_LPAREN, "expected '('")) goto fail;

    /* init: variable declaration or expression statement (optional) */
    if (tracey_parser_check(parser, TRACEY_TOKEN_SEMICOLON))
    {
        s->as.for_stmt.init = NULL;
    }
    else if (tracey_parser_check(parser, TRACEY_TOKEN_KEYWORD_INT) ||
             tracey_parser_check(parser, TRACEY_TOKEN_KEYWORD_BOOL) ||
             tracey_parser_check(parser, TRACEY_TOKEN_KEYWORD_CHAR) ||
             tracey_parser_check(parser, TRACEY_TOKEN_KEYWORD_LONG) ||
             tracey_parser_check(parser, TRACEY_TOKEN_KEYWORD_SHORT) ||
             tracey_parser_check(parser, TRACEY_TOKEN_KEYWORD_UNSIGNED) ||
             tracey_parser_check(parser, TRACEY_TOKEN_KEYWORD_SIGNED) ||
             tracey_parser_check(parser, TRACEY_TOKEN_KEYWORD_FLOAT) ||
             tracey_parser_check(parser, TRACEY_TOKEN_KEYWORD_DOUBLE) ||
             tracey_parser_check(parser, TRACEY_TOKEN_KEYWORD_VOID) ||
             tracey_parser_check(parser, TRACEY_TOKEN_KEYWORD_SIZE_T) ||
             tracey_parser_check(parser, TRACEY_TOKEN_KEYWORD_STRUCT))
    {
        s->as.for_stmt.init = tracey_parse_var_decl_as_stmt(parser);
        if (!s->as.for_stmt.init) goto fail;
    }
    else
    {
        struct tracey_expr *e = tracey_parse_expression(parser);
        if (!e) goto fail;
        s->as.for_stmt.init = tracey_stmt_create(TRACEY_STMT_EXPR, e->loc);
        if (!s->as.for_stmt.init) { tracey_expr_destroy(e); goto fail; }
        s->as.for_stmt.init->as.expr_stmt.expression = e;
    }

    if (!tracey_parser_expect(parser, TRACEY_TOKEN_SEMICOLON, "expected ';' after for init")) goto fail;

    /* condition (optional) */
    if (tracey_parser_check(parser, TRACEY_TOKEN_SEMICOLON))
    {
        s->as.for_stmt.condition = NULL;
    }
    else
    {
        s->as.for_stmt.condition = tracey_parse_expression(parser);
        if (!s->as.for_stmt.condition) goto fail;
        if (!tracey_parser_expect(parser, TRACEY_TOKEN_SEMICOLON, "expected ';' after for condition")) goto fail;
    }

    /* update (optional) */
    if (tracey_parser_check(parser, TRACEY_TOKEN_RPAREN))
    {
        s->as.for_stmt.update = NULL;
    }
    else
    {
        s->as.for_stmt.update = tracey_parse_expression(parser);
        if (!s->as.for_stmt.update) goto fail;
    }

    if (!tracey_parser_expect(parser, TRACEY_TOKEN_RPAREN, "expected ')' after for update")) goto fail;

    s->as.for_stmt.body = tracey_parse_statement(parser);
    if (!s->as.for_stmt.body) goto fail;
    return s;
fail:
    tracey_stmt_destroy(s);
    return NULL;
}

static struct tracey_stmt *tracey_parse_break(struct tracey_parser *parser)
{
    struct tracey_source_loc loc = {parser->current.line, parser->current.column};
    struct tracey_stmt *s = tracey_stmt_create(TRACEY_STMT_BREAK, loc);
    if (!s) return NULL;
    tracey_parser_advance(parser);
    if (!tracey_parser_expect(parser, TRACEY_TOKEN_SEMICOLON, "expected ';'")) { tracey_stmt_destroy(s); return NULL; }
    return s;
}

static struct tracey_stmt *tracey_parse_continue(struct tracey_parser *parser)
{
    struct tracey_source_loc loc = {parser->current.line, parser->current.column};
    struct tracey_stmt *s = tracey_stmt_create(TRACEY_STMT_CONTINUE, loc);
    if (!s) return NULL;
    tracey_parser_advance(parser);
    if (!tracey_parser_expect(parser, TRACEY_TOKEN_SEMICOLON, "expected ';'")) { tracey_stmt_destroy(s); return NULL; }
    return s;
}

static struct tracey_stmt *tracey_parse_goto(struct tracey_parser *parser)
{
    struct tracey_source_loc loc = {parser->current.line, parser->current.column};
    struct tracey_stmt *s = tracey_stmt_create(TRACEY_STMT_GOTO, loc);
    if (!s) return NULL;
    tracey_parser_advance(parser);
    if (!tracey_parser_check(parser, TRACEY_TOKEN_IDENTIFIER))
    {
        tracey_stmt_destroy(s);
        tracey_parser_error(parser, "Expected label name after 'goto'", &parser->current);
        return NULL;
    }
    s->as.goto_stmt.label = tracey_token_strdup(&parser->current);
    tracey_parser_advance(parser);
    if (!tracey_parser_expect(parser, TRACEY_TOKEN_SEMICOLON, "expected ';'"))
    {
        free(s->as.goto_stmt.label);
        tracey_stmt_destroy(s);
        return NULL;
    }
    return s;
}

static struct tracey_stmt *tracey_parse_return(struct tracey_parser *parser)
{
    struct tracey_source_loc loc = {parser->current.line, parser->current.column};
    struct tracey_stmt *s = tracey_stmt_create(TRACEY_STMT_RETURN, loc);
    if (!s) return NULL;
    tracey_parser_advance(parser);
    if (!tracey_parser_check(parser, TRACEY_TOKEN_SEMICOLON))
    {
        s->as.return_stmt.value = tracey_parse_expression(parser);
        if (!s->as.return_stmt.value) goto fail;
    }
    if (!tracey_parser_expect(parser, TRACEY_TOKEN_SEMICOLON, "expected ';'")) goto fail;
    return s;
fail:
    tracey_stmt_destroy(s);
    return NULL;
}

static struct tracey_stmt *tracey_parse_statement(struct tracey_parser *parser)
{
    /* Label: identifier: statement */
    if (tracey_parser_check(parser, TRACEY_TOKEN_IDENTIFIER))
    {
        struct tracey_token next = tracey_lexer_peek(parser->lexer);
        if (next.type == TRACEY_TOKEN_COLON)
        {
            struct tracey_token name_tok = parser->current;
            tracey_parser_advance(parser); /* consume identifier */
            tracey_parser_advance(parser); /* consume ':' */
            {
                struct tracey_stmt *s = tracey_stmt_create(TRACEY_STMT_LABEL,
                    (struct tracey_source_loc){name_tok.line, name_tok.column});
                if (!s) return NULL;
                s->as.label_stmt.label = tracey_token_strdup(&name_tok);
                s->as.label_stmt.stmt = tracey_parse_statement(parser);
                if (!s->as.label_stmt.stmt) { tracey_stmt_destroy(s); return NULL; }
                return s;
            }
        }
    }

    if (tracey_parser_check(parser, TRACEY_TOKEN_LBRACE))
        return tracey_parse_block(parser);
    if (tracey_parser_check(parser, TRACEY_TOKEN_KEYWORD_IF))
        return tracey_parse_if(parser);
    if (tracey_parser_check(parser, TRACEY_TOKEN_KEYWORD_WHILE))
        return tracey_parse_while(parser);
    if (tracey_parser_check(parser, TRACEY_TOKEN_KEYWORD_DO))
        return tracey_parse_do(parser);
    if (tracey_parser_check(parser, TRACEY_TOKEN_KEYWORD_SWITCH))
        return tracey_parse_switch(parser);
    if (tracey_parser_check(parser, TRACEY_TOKEN_KEYWORD_FOR))
        return tracey_parse_for(parser);
    if (tracey_parser_check(parser, TRACEY_TOKEN_KEYWORD_BREAK))
        return tracey_parse_break(parser);
    if (tracey_parser_check(parser, TRACEY_TOKEN_KEYWORD_CONTINUE))
        return tracey_parse_continue(parser);
    if (tracey_parser_check(parser, TRACEY_TOKEN_KEYWORD_GOTO))
        return tracey_parse_goto(parser);
    if (tracey_parser_check(parser, TRACEY_TOKEN_KEYWORD_RETURN))
        return tracey_parse_return(parser);
    if (tracey_parser_check(parser, TRACEY_TOKEN_KEYWORD_INT) ||
        tracey_parser_check(parser, TRACEY_TOKEN_KEYWORD_BOOL) ||
        tracey_parser_check(parser, TRACEY_TOKEN_KEYWORD_VOID) ||
        tracey_parser_check(parser, TRACEY_TOKEN_KEYWORD_CHAR) ||
        tracey_parser_check(parser, TRACEY_TOKEN_KEYWORD_FLOAT) ||
        tracey_parser_check(parser, TRACEY_TOKEN_KEYWORD_DOUBLE) ||
        tracey_parser_check(parser, TRACEY_TOKEN_KEYWORD_LONG) ||
        tracey_parser_check(parser, TRACEY_TOKEN_KEYWORD_SHORT) ||
        tracey_parser_check(parser, TRACEY_TOKEN_KEYWORD_UNSIGNED) ||
        tracey_parser_check(parser, TRACEY_TOKEN_KEYWORD_SIGNED) ||
        tracey_parser_check(parser, TRACEY_TOKEN_KEYWORD_SIZE_T) ||
        tracey_parser_check(parser, TRACEY_TOKEN_KEYWORD_STRUCT) ||
        tracey_parser_is_typedef_alias(parser))
    {
        int struct_id = -1;
        enum tracey_type_kind t = tracey_parse_type_ex(parser, &struct_id);
        struct tracey_token name_tok;
        struct tracey_stmt *vd;
        if (!tracey_parser_check(parser, TRACEY_TOKEN_IDENTIFIER))
        {
            tracey_parser_error(parser, "Expected variable name", &parser->current);
            return NULL;
        }
        name_tok = parser->current;
        vd = tracey_stmt_create(TRACEY_STMT_VAR_DECL, (struct tracey_source_loc){name_tok.line, name_tok.column});
        if (!vd) return NULL;
        vd->as.var_decl_stmt.type = t;
        vd->as.var_decl_stmt.struct_id = struct_id;
        vd->as.var_decl_stmt.name = tracey_token_strdup(&name_tok);
        tracey_parser_advance(parser);
        if (tracey_parser_check(parser, TRACEY_TOKEN_LBRACKET))
        {
            tracey_parser_advance(parser);
            if (tracey_parser_check(parser, TRACEY_TOKEN_INTEGER))
            {
                unsigned long long sv = 0;
                struct tracey_token sztok = parser->current;
                if (!tracey_parse_int_literal(&sztok, &sv))
                    tracey_parser_error(parser, "Invalid array size", &sztok);
                vd->as.var_decl_stmt.array_size = (size_t)sv;
                tracey_parser_advance(parser);
            }
            if (!tracey_parser_expect(parser, TRACEY_TOKEN_RBRACKET, "expected ']'"))
            { tracey_stmt_destroy(vd); return NULL; }
        }
        if (tracey_parser_match(parser, TRACEY_TOKEN_ASSIGN))
        {
            vd->as.var_decl_stmt.init = tracey_parse_expression(parser);
            if (!vd->as.var_decl_stmt.init) { tracey_stmt_destroy(vd); return NULL; }
        }
        if (!tracey_parser_expect(parser, TRACEY_TOKEN_SEMICOLON, "expected ';'"))
        { tracey_stmt_destroy(vd); return NULL; }
        return vd;
    }
    if (tracey_parser_check(parser, TRACEY_TOKEN_SEMICOLON))
    {
        tracey_parser_advance(parser);
        return tracey_stmt_create(TRACEY_STMT_BLOCK, (struct tracey_source_loc){1,1});
    }
    {
        struct tracey_expr *e = tracey_parse_expression(parser);
        struct tracey_stmt *es;
        if (!e) return NULL;
        if (!tracey_parser_expect(parser, TRACEY_TOKEN_SEMICOLON, "expected ';'"))
        { tracey_expr_destroy(e); return NULL; }
        es = tracey_stmt_create(TRACEY_STMT_EXPR, e->loc);
        if (!es) { tracey_expr_destroy(e); return NULL; }
        es->as.expr_stmt.expression = e;
        return es;
    }
}

static bool tracey_parse_function(struct tracey_parser *parser, struct tracey_program *program)
{
    struct tracey_source_loc loc = {parser->current.line, parser->current.column};
    enum tracey_type_kind rt = tracey_parse_type(parser);
    struct tracey_function_decl *fn;
    struct tracey_token name_tok;

    if (!tracey_parser_check(parser, TRACEY_TOKEN_IDENTIFIER))
    {
        tracey_parser_error(parser, "Expected function name", &parser->current);
        return false;
    }
    name_tok = parser->current;
    tracey_parser_advance(parser);
    if (!tracey_parser_expect(parser, TRACEY_TOKEN_LPAREN, "expected '('"))
        return false;

    fn = tracey_function_decl_create();
    if (!fn) return false;
    fn->return_type = rt;
    fn->name = tracey_token_strdup(&name_tok);
    fn->loc = loc;

    if (!tracey_parser_check(parser, TRACEY_TOKEN_RPAREN))
    {
        for (;;)
        {
            if (tracey_parser_check(parser, TRACEY_TOKEN_DOT))
            {
                tracey_parser_match(parser, TRACEY_TOKEN_DOT);
                tracey_parser_match(parser, TRACEY_TOKEN_DOT);
                tracey_parser_match(parser, TRACEY_TOKEN_DOT);
                fn->variadic = true;
                break;
            }
            {
                enum tracey_type_kind pt = tracey_parse_type(parser);
                struct tracey_param *new_params;
                if (pt == TRACEY_TYPE_VOID && !tracey_parser_check(parser, TRACEY_TOKEN_IDENTIFIER))
                {
                    if (fn->param_count > 0 || fn->variadic)
                        tracey_parser_error(parser, "Void parameter must be the only parameter", &parser->current);
                    break;
                }
                if (!tracey_parser_check(parser, TRACEY_TOKEN_IDENTIFIER))
                {
                    tracey_parser_error(parser, "Expected parameter name", &parser->current);
                    break;
                }
                new_params = (struct tracey_param *)realloc(fn->params, (fn->param_count + 1) * sizeof(*new_params));
                if (!new_params) { tracey_function_decl_destroy(fn); return false; }
                fn->params = new_params;
                fn->params[fn->param_count].type = pt;
                fn->params[fn->param_count].name = tracey_token_strdup(&parser->current);
                fn->param_count++;
                tracey_parser_advance(parser);
            }
            if (!tracey_parser_match(parser, TRACEY_TOKEN_COMMA))
                break;
        }
    }
    if (!tracey_parser_expect(parser, TRACEY_TOKEN_RPAREN, "expected ')'"))
    { tracey_function_decl_destroy(fn); return false; }

    if (tracey_parser_check(parser, TRACEY_TOKEN_SEMICOLON))
    {
        tracey_parser_advance(parser);
        fn->body = NULL;
    }
    else
    {
        fn->body = tracey_parse_block(parser);
        if (!fn->body) { tracey_function_decl_destroy(fn); return false; }
    }

    if (!tracey_program_push_function(program, fn))
    { tracey_function_decl_destroy(fn); return false; }
    return true;
}

bool tracey_parser_parse(struct tracey_parser *parser, struct tracey_program *out)
{
    if (parser == NULL || out == NULL)
        return false;
    tracey_program_init(out);
    parser->program = out;

    while (parser->has_current && parser->current.type != TRACEY_TOKEN_EOF)
    {
        if (tracey_parser_check(parser, TRACEY_TOKEN_KEYWORD_TYPEDEF))
        {
            if (!tracey_parse_typedef(parser))
            {
                tracey_parser_synchronize(parser);
                if (tracey_parser_check(parser, TRACEY_TOKEN_SEMICOLON))
                    tracey_parser_advance(parser);
            }
            continue;
        }
        if (tracey_parser_check(parser, TRACEY_TOKEN_KEYWORD_STRUCT))
        {
            /*
             * At file scope a struct tag can only introduce a definition;
             * struct-typed global variables and struct return types are not
             * supported yet, so the parser expects the brace-enclosed body
             * directly and reports an error otherwise.
             */
            if (!tracey_parse_struct_definition(parser))
            {
                tracey_parser_synchronize(parser);
                if (tracey_parser_check(parser, TRACEY_TOKEN_SEMICOLON))
                    tracey_parser_advance(parser);
            }
            continue;
        }
        if (parser->current.type == TRACEY_TOKEN_ERROR)
        {
            tracey_parser_error(parser, "Unexpected lexer error", &parser->current);
            tracey_parser_advance(parser);
            continue;
        }
        if (!tracey_parse_function(parser, out))
        {
            tracey_parser_synchronize(parser);
            if (tracey_parser_check(parser, TRACEY_TOKEN_SEMICOLON))
                tracey_parser_advance(parser);
            if (tracey_parser_check(parser, TRACEY_TOKEN_RBRACE))
                tracey_parser_advance(parser);
        }
    }
    return !parser->had_error;
}
