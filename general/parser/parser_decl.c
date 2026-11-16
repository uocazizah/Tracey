#include "parser_internal.h"

bool tracey_program_push_function(struct tracey_program *program, struct tracey_function_decl *fn)
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

struct tracey_expr *tracey_parse_expression(struct tracey_parser *parser);
struct tracey_stmt *tracey_parse_statement(struct tracey_parser *parser);
struct tracey_stmt *tracey_parse_block(struct tracey_parser *parser);
struct tracey_stmt *tracey_parse_for(struct tracey_parser *parser);
struct tracey_stmt *tracey_parse_break(struct tracey_parser *parser);
struct tracey_stmt *tracey_parse_continue(struct tracey_parser *parser);
struct tracey_stmt *tracey_parse_goto(struct tracey_parser *parser);

/* Forward declarations for expression precedence */
struct tracey_expr *tracey_parse_assignment(struct tracey_parser *parser);
struct tracey_expr *tracey_parse_ternary(struct tracey_parser *parser);
struct tracey_expr *tracey_parse_logical_or(struct tracey_parser *parser);
struct tracey_expr *tracey_parse_logical_and(struct tracey_parser *parser);
struct tracey_expr *tracey_parse_bitwise_or(struct tracey_parser *parser);
struct tracey_expr *tracey_parse_bitwise_xor(struct tracey_parser *parser);
struct tracey_expr *tracey_parse_bitwise_and(struct tracey_parser *parser);
struct tracey_expr *tracey_parse_equality(struct tracey_parser *parser);
struct tracey_expr *tracey_parse_relational(struct tracey_parser *parser);
struct tracey_expr *tracey_parse_additive(struct tracey_parser *parser);
struct tracey_expr *tracey_parse_multiplicative(struct tracey_parser *parser);
struct tracey_expr *tracey_parse_unary(struct tracey_parser *parser);
struct tracey_expr *tracey_parse_primary(struct tracey_parser *parser);


bool tracey_parse_function(struct tracey_parser *parser, struct tracey_program *program)
{
    struct tracey_source_loc loc = {parser->current.line, parser->current.column};
    int return_sid = -1;
    struct tracey_func_sig *return_fn_sig = NULL;
    enum tracey_type_kind rt = tracey_parse_type_ex(parser, &return_sid, &return_fn_sig);
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
    fn->return_struct_id = return_sid;
    fn->return_fn_sig = return_fn_sig;
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
                int psid = -1;
                struct tracey_func_sig *psig = NULL;
                enum tracey_type_kind pt = tracey_parse_type_ex(parser, &psid, &psig);
                struct tracey_token pname_tok;
                struct tracey_param *new_params;

                /* Function pointer parameter declarator. */
                if (tracey_parser_check(parser, TRACEY_TOKEN_LPAREN))
                {
                    struct tracey_token pk = tracey_lexer_peek(parser->lexer);
                    if (pk.type == TRACEY_TOKEN_STAR)
                    {
                        tracey_parser_advance(parser); /* '(' */
                        tracey_parser_advance(parser); /* '*' */
                        if (!tracey_parser_check(parser, TRACEY_TOKEN_IDENTIFIER))
                        {
                            tracey_parser_error(parser, "Expected parameter name", &parser->current);
                            break;
                        }
                        pname_tok = parser->current;
                        tracey_parser_advance(parser);
                        if (!tracey_parser_expect(parser, TRACEY_TOKEN_RPAREN, "expected ')'"))
                            break;
                        if (!tracey_parser_expect(parser, TRACEY_TOKEN_LPAREN, "expected '('"))
                            break;
                        psig = (struct tracey_func_sig *)calloc(1, sizeof(*psig));
                        if (psig == NULL) { tracey_function_decl_destroy(fn); return false; }
                        psig->return_type = pt;
                        if (!tracey_parser_check(parser, TRACEY_TOKEN_RPAREN))
                        {
                            for (;;)
                            {
                                enum tracey_type_kind ppt;
                                int ppsid = -1;
                                enum tracey_type_kind *np;
                                if (tracey_parser_check(parser, TRACEY_TOKEN_DOT))
                                {
                                    tracey_parser_match(parser, TRACEY_TOKEN_DOT);
                                    tracey_parser_match(parser, TRACEY_TOKEN_DOT);
                                    tracey_parser_match(parser, TRACEY_TOKEN_DOT);
                                    break;
                                }
                                ppt = tracey_parse_type_ex(parser, &ppsid, NULL);
                                if (tracey_parser_check(parser, TRACEY_TOKEN_IDENTIFIER))
                                    tracey_parser_advance(parser);
                                np = (enum tracey_type_kind *)realloc(psig->param_types,
                                                                       (psig->param_count + 1) * sizeof(*np));
                                if (np == NULL)
                                {
                                    tracey_func_sig_free(psig);
                                    tracey_function_decl_destroy(fn);
                                    return false;
                                }
                                psig->param_types = np;
                                psig->param_types[psig->param_count++] = ppt;
                                if (!tracey_parser_match(parser, TRACEY_TOKEN_COMMA))
                                    break;
                            }
                        }
                        if (!tracey_parser_expect(parser, TRACEY_TOKEN_RPAREN, "expected ')'"))
                        {
                            tracey_func_sig_free(psig);
                            break;
                        }
                        pt = TRACEY_TYPE_FUNC_PTR;
                    }
                }

                if (pt == TRACEY_TYPE_VOID && !tracey_parser_check(parser, TRACEY_TOKEN_IDENTIFIER) &&
                    psig == NULL)
                {
                    if (fn->param_count > 0 || fn->variadic)
                        tracey_parser_error(parser, "Void parameter must be the only parameter", &parser->current);
                    break;
                }
                if (psig == NULL)
                {
                    if (!tracey_parser_check(parser, TRACEY_TOKEN_IDENTIFIER))
                    {
                        tracey_parser_error(parser, "Expected parameter name", &parser->current);
                        break;
                    }
                    pname_tok = parser->current;
                }
                new_params = (struct tracey_param *)realloc(fn->params, (fn->param_count + 1) * sizeof(*new_params));
                if (!new_params) { tracey_function_decl_destroy(fn); return false; }
                fn->params = new_params;
                fn->params[fn->param_count].type = pt;
                fn->params[fn->param_count].struct_id = psid;
                fn->params[fn->param_count].fn_sig = psig;
                fn->params[fn->param_count].name = tracey_token_strdup(&pname_tok);
                fn->param_count++;
                if (psig == NULL)
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
    { tracey_function_decl_destroy(fn); fprintf(stderr,"pf push fail\n"); fflush(stderr); return false; }
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
        bool _is_struct_def = false;
        if (tracey_parser_check(parser, TRACEY_TOKEN_KEYWORD_STRUCT))
            {
                struct tracey_lexer _lex = *parser->lexer;
                struct tracey_token _tag = tracey_lexer_next(&_lex);
                struct tracey_token _next = tracey_lexer_next(&_lex);
                if (_tag.type == TRACEY_TOKEN_IDENTIFIER && _next.type == TRACEY_TOKEN_LBRACE)
                    _is_struct_def = true;
            }
            if (_is_struct_def)
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

