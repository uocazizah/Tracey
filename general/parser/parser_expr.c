#include "parser_internal.h"

struct tracey_expr *tracey_parse_expression(struct tracey_parser *parser)
{
    return tracey_parse_assignment(parser);
}


struct tracey_expr *tracey_parse_primary(struct tracey_parser *parser)
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


struct tracey_expr *tracey_parse_unary(struct tracey_parser *parser)
{
    if (tracey_parser_check(parser, TRACEY_TOKEN_KEYWORD_SIZEOF))
    {
        struct tracey_source_loc loc = {parser->current.line, parser->current.column};
        struct tracey_expr *result;
        bool looks_type = false;
        tracey_parser_advance(parser); /* consume sizeof */
        if (tracey_parser_check(parser, TRACEY_TOKEN_LPAREN))
        {
            tracey_parser_advance(parser); /* consume '(' */
            /* Decide whether the parenthesised operand is a type name. */
            if (tracey_parser_check(parser, TRACEY_TOKEN_KEYWORD_STRUCT) ||
                tracey_parser_check(parser, TRACEY_TOKEN_KEYWORD_INT) ||
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
                looks_type = true;
            }
            else if (tracey_parser_check(parser, TRACEY_TOKEN_IDENTIFIER))
            {
                size_t ti;
                for (ti = 0; ti < parser->typedef_count; ti++)
                {
                    if (strlen(parser->typedef_names[ti]) == parser->current.lexeme_len &&
                        memcmp(parser->typedef_names[ti], parser->current.lexeme, parser->current.lexeme_len) == 0)
                    {
                        looks_type = true;
                        break;
                    }
                }
                if (!looks_type && parser->program != NULL)
                {
                    char *tag = tracey_token_strdup(&parser->current);
                    if (tag != NULL && tracey_program_find_struct_id(parser->program, tag) >= 0)
                        looks_type = true;
                    free(tag);
                }
            }

            result = tracey_expr_create(TRACEY_EXPR_SIZEOF, loc);
            if (result == NULL)
                return NULL;
            if (looks_type)
            {
                int sid = -1;
                enum tracey_type_kind t = tracey_parse_type_ex(parser, &sid, NULL);
                result->as.sizeof_expr.is_type = true;
                result->as.sizeof_expr.type = t;
                result->as.sizeof_expr.struct_id = sid;
                result->as.sizeof_expr.fn_sig = NULL;
                result->as.sizeof_expr.operand = NULL;
            }
            else
            {
                struct tracey_expr *inner = tracey_parse_expression(parser);
                result->as.sizeof_expr.is_type = false;
                result->as.sizeof_expr.type = TRACEY_TYPE_VOID;
                result->as.sizeof_expr.struct_id = -1;
                result->as.sizeof_expr.fn_sig = NULL;
                result->as.sizeof_expr.operand = inner;
            }
            if (!tracey_parser_expect(parser, TRACEY_TOKEN_RPAREN, "expected ')'"))
            {
                tracey_expr_destroy(result);
                return NULL;
            }
            return result;
        }
        else
        {
            struct tracey_expr *inner = tracey_parse_unary(parser);
            result = tracey_expr_create(TRACEY_EXPR_SIZEOF, loc);
            if (result == NULL)
            {
                tracey_expr_destroy(inner);
                return NULL;
            }
            result->as.sizeof_expr.is_type = false;
            result->as.sizeof_expr.type = TRACEY_TYPE_VOID;
            result->as.sizeof_expr.struct_id = -1;
            result->as.sizeof_expr.fn_sig = NULL;
            result->as.sizeof_expr.operand = inner;
            return result;
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


struct tracey_expr *tracey_parse_multiplicative(struct tracey_parser *parser)
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


struct tracey_expr *tracey_parse_additive(struct tracey_parser *parser)
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


struct tracey_expr *tracey_parse_shift(struct tracey_parser *parser)
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


struct tracey_expr *tracey_parse_relational(struct tracey_parser *parser)
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


struct tracey_expr *tracey_parse_equality(struct tracey_parser *parser)
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


struct tracey_expr *tracey_parse_logical_and(struct tracey_parser *parser)
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


struct tracey_expr *tracey_parse_bitwise_or(struct tracey_parser *parser)
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


struct tracey_expr *tracey_parse_bitwise_xor(struct tracey_parser *parser)
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


struct tracey_expr *tracey_parse_bitwise_and(struct tracey_parser *parser)
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


struct tracey_expr *tracey_parse_logical_or(struct tracey_parser *parser)
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


struct tracey_expr *tracey_parse_assignment(struct tracey_parser *parser)
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


struct tracey_expr *tracey_parse_ternary(struct tracey_parser *parser)
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


