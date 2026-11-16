#include "parser_internal.h"

bool tracey_parser_is_typedef_alias(const struct tracey_parser *parser)
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


enum tracey_type_kind tracey_parse_type_ex(struct tracey_parser *parser,
                                                  int *out_struct_id,
                                                  struct tracey_func_sig **out_fn_sig);


enum tracey_type_kind tracey_parse_type(struct tracey_parser *parser)
{
    return tracey_parse_type_ex(parser, NULL, NULL);
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

enum tracey_type_kind tracey_parse_type_ex(struct tracey_parser *parser,
                                                  int *out_struct_id,
                                                  struct tracey_func_sig **out_fn_sig)
{
    if (out_struct_id != NULL)
        *out_struct_id = -1;
    if (out_fn_sig != NULL)
        *out_fn_sig = NULL;

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
            tracey_parser_advance(parser);
            if (out_struct_id != NULL)
                *out_struct_id = sid;
            return TRACEY_TYPE_STRUCT_PTR;
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
                if (out_fn_sig != NULL && parser->typedef_fn_sigs[_ti] != NULL)
                {
                    struct tracey_func_sig *sig = (struct tracey_func_sig *)calloc(1, sizeof(*sig));
                    if (sig != NULL)
                    {
                        sig->return_type = parser->typedef_fn_sigs[_ti]->return_type;
                        if (parser->typedef_fn_sigs[_ti]->param_count > 0)
                        {
                            sig->param_types = (enum tracey_type_kind *)malloc(parser->typedef_fn_sigs[_ti]->param_count * sizeof(*sig->param_types));
                            if (sig->param_types != NULL)
                            {
                                memcpy(sig->param_types, parser->typedef_fn_sigs[_ti]->param_types, parser->typedef_fn_sigs[_ti]->param_count * sizeof(*sig->param_types));
                                sig->param_count = parser->typedef_fn_sigs[_ti]->param_count;
                            }
                        }
                        *out_fn_sig = sig;
                    }
                }
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


bool tracey_parse_struct_definition(struct tracey_parser *parser)
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

        mt = tracey_parse_type_ex(parser, &member_sid, NULL);
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
            size_t malign = 8;

            switch (mt)
            {
                case TRACEY_TYPE_CHAR:
                case TRACEY_TYPE_BOOL:
                    msize = 1;
                    malign = 1;
                    break;
                case TRACEY_TYPE_SHORT:
                case TRACEY_TYPE_UNSIGNED_SHORT:
                    msize = 2;
                    malign = 2;
                    break;
                case TRACEY_TYPE_INT:
                case TRACEY_TYPE_UNSIGNED_INT:
                case TRACEY_TYPE_FLOAT:
                    msize = 4;
                    malign = 4;
                    break;
                case TRACEY_TYPE_STRUCT:
                    if (member_sid >= 0 &&
                        (size_t)member_sid < parser->program->struct_count)
                    {
                        msize = parser->program->structs[member_sid].size;
                        malign = parser->program->structs[member_sid].alignment;
                        if (malign == 0)
                            malign = 1;
                    }
                    break;
                default:
                    msize = 8;
                    malign = 8;
                    break;
            }

            if (!tracey_struct_add_member(&parser->program->structs[sid], mname, mt,
                                          member_sid, msize, malign))
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


bool tracey_parser_add_typedef(struct tracey_parser *parser, const char *name,
                                         enum tracey_type_kind type,
                                         struct tracey_func_sig *fn_sig)
{
    if (parser->typedef_count == parser->typedef_cap)
    {
        size_t cap = parser->typedef_cap ? parser->typedef_cap * 2 : 8;
        char **nn = (char **)realloc(parser->typedef_names, cap * sizeof(*nn));
        enum tracey_type_kind *tt = (enum tracey_type_kind *)realloc(parser->typedef_types, cap * sizeof(*tt));
        struct tracey_func_sig **ts = (struct tracey_func_sig **)realloc(parser->typedef_fn_sigs, cap * sizeof(*ts));
        if (!nn || !tt || !ts) return false;
        parser->typedef_names = nn;
        parser->typedef_types = tt;
        parser->typedef_fn_sigs = ts;
        parser->typedef_cap = cap;
    }
    parser->typedef_names[parser->typedef_count] = strdup(name);
    parser->typedef_types[parser->typedef_count] = type;
    parser->typedef_fn_sigs[parser->typedef_count] = fn_sig;
    parser->typedef_count++;
    return true;
}


bool tracey_parse_typedef(struct tracey_parser *parser)
{
    enum tracey_type_kind t;
    struct tracey_token name_tok;
    struct tracey_func_sig *fn_sig = NULL;
    tracey_parser_advance(parser); /* typedef */
    t = tracey_parse_type_ex(parser, NULL, NULL);
    /* Function pointer typedef declarator: typedef T (*name)(params); */
    if (tracey_parser_check(parser, TRACEY_TOKEN_LPAREN))
    {
        struct tracey_token pk = tracey_lexer_peek(parser->lexer);
        if (pk.type == TRACEY_TOKEN_STAR)
        {
            tracey_parser_advance(parser); /* '(' */
            tracey_parser_advance(parser); /* '*' */
            if (!tracey_parser_check(parser, TRACEY_TOKEN_IDENTIFIER))
            {
                tracey_parser_error(parser, "Expected typedef name", &parser->current);
                return false;
            }
            name_tok = parser->current;
            tracey_parser_advance(parser);
            if (!tracey_parser_expect(parser, TRACEY_TOKEN_RPAREN, "expected ')'"))
                return false;
            if (!tracey_parser_expect(parser, TRACEY_TOKEN_LPAREN, "expected '('"))
                return false;
            fn_sig = (struct tracey_func_sig *)calloc(1, sizeof(*fn_sig));
            if (fn_sig == NULL) return false;
            fn_sig->return_type = t;
            if (!tracey_parser_check(parser, TRACEY_TOKEN_RPAREN))
            {
                for (;;)
                {
                    enum tracey_type_kind pt;
                    int psid = -1;
                    if (tracey_parser_check(parser, TRACEY_TOKEN_DOT))
                    {
                        tracey_parser_match(parser, TRACEY_TOKEN_DOT);
                        tracey_parser_match(parser, TRACEY_TOKEN_DOT);
                        tracey_parser_match(parser, TRACEY_TOKEN_DOT);
                        break;
                    }
                    pt = tracey_parse_type_ex(parser, &psid, NULL);
                    if (tracey_parser_check(parser, TRACEY_TOKEN_IDENTIFIER))
                        tracey_parser_advance(parser);
                    {
                        enum tracey_type_kind *np = (enum tracey_type_kind *)realloc(fn_sig->param_types,
                            (fn_sig->param_count + 1) * sizeof(*np));
                        if (np == NULL) { tracey_func_sig_free(fn_sig); return false; }
                        fn_sig->param_types = np;
                        fn_sig->param_types[fn_sig->param_count++] = pt;
                    }
                    if (!tracey_parser_match(parser, TRACEY_TOKEN_COMMA))
                        break;
                }
            }
            if (!tracey_parser_expect(parser, TRACEY_TOKEN_RPAREN, "expected ')'"))
            {
                tracey_func_sig_free(fn_sig);
                return false;
            }
            t = TRACEY_TYPE_FUNC_PTR;
        }
        else
        {
            if (!tracey_parser_check(parser, TRACEY_TOKEN_IDENTIFIER))
            {
                tracey_parser_error(parser, "Expected typedef name", &parser->current);
                return false;
            }
            name_tok = parser->current;
        }
    }
    else
    {
        if (!tracey_parser_check(parser, TRACEY_TOKEN_IDENTIFIER))
        {
            tracey_parser_error(parser, "Expected typedef name", &parser->current);
            return false;
        }
        name_tok = parser->current;
    }
    {
        char *nm = tracey_token_strdup(&name_tok);
        tracey_parser_add_typedef(parser, nm, t, fn_sig);
        free(nm);
    }
    if (fn_sig == NULL)
        tracey_parser_advance(parser);
    if (!tracey_parser_expect(parser, TRACEY_TOKEN_SEMICOLON, "expected ';'"))
        return false;
    return true;
}


