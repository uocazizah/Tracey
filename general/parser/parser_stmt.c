#include "parser_internal.h"

bool tracey_block_push(struct tracey_block_stmt *block, struct tracey_stmt *stmt)
{
    struct tracey_stmt **new_items = (struct tracey_stmt **)realloc(block->statements,
                                                                    (block->count + 1) * sizeof(*new_items));
    if (!new_items) return false;
    new_items[block->count++] = stmt;
    block->statements = new_items;
    return true;
}


struct tracey_stmt *tracey_parse_block(struct tracey_parser *parser)
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


struct tracey_stmt *tracey_parse_if(struct tracey_parser *parser)
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


struct tracey_stmt *tracey_parse_while(struct tracey_parser *parser)
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


struct tracey_stmt *tracey_parse_do(struct tracey_parser *parser)
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


struct tracey_stmt *tracey_parse_switch(struct tracey_parser *parser)
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

struct tracey_stmt *tracey_parse_var_decl_as_stmt(struct tracey_parser *parser)
{
    int struct_id = -1;
    struct tracey_func_sig *fn_sig = NULL;
    enum tracey_type_kind t = tracey_parse_type_ex(parser, &struct_id, &fn_sig);
    struct tracey_token name_tok;
    struct tracey_stmt *vd;
    bool have_name = false;

    /* Function pointer declarator: 'type' '(' '*' name? ')' '(' params ')'. */
    if (tracey_parser_check(parser, TRACEY_TOKEN_LPAREN))
    {
        struct tracey_token peek = tracey_lexer_peek(parser->lexer);


        if (peek.type == TRACEY_TOKEN_STAR)
        {
            tracey_parser_advance(parser); /* '(' */
            tracey_parser_advance(parser); /* '*' */
            if (!tracey_parser_check(parser, TRACEY_TOKEN_IDENTIFIER))
            {
                tracey_parser_error(parser, "Expected function pointer name", &parser->current);
                tracey_func_sig_free(fn_sig);
                return NULL;
            }
            name_tok = parser->current;
            have_name = true;
            tracey_parser_advance(parser);
            if (!tracey_parser_expect(parser, TRACEY_TOKEN_RPAREN, "expected ')'"))
                return NULL;
            if (!tracey_parser_expect(parser, TRACEY_TOKEN_LPAREN, "expected '('"))
                return NULL;

            fn_sig = (struct tracey_func_sig *)calloc(1, sizeof(*fn_sig));
            if (fn_sig == NULL)
                return NULL;
            fn_sig->return_type = t;
            if (!tracey_parser_check(parser, TRACEY_TOKEN_RPAREN))
            {
                for (;;)
                {
                    enum tracey_type_kind pt;
                    int psid = -1;
                    enum tracey_type_kind *np;

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
                    np = (enum tracey_type_kind *)realloc(fn_sig->param_types,
                                                           (fn_sig->param_count + 1) * sizeof(*np));
                    if (np == NULL)
                    {
                        tracey_func_sig_free(fn_sig);
                        return NULL;
                    }
                    fn_sig->param_types = np;
                    fn_sig->param_types[fn_sig->param_count++] = pt;
                    if (!tracey_parser_match(parser, TRACEY_TOKEN_COMMA))
                        break;
                }
            }
            if (!tracey_parser_expect(parser, TRACEY_TOKEN_RPAREN, "expected ')'"))
            {
                tracey_func_sig_free(fn_sig);
                return NULL;
            }
            t = TRACEY_TYPE_FUNC_PTR;
            struct_id = -1;
        }
    }

    if (!have_name)
    {
        if (!tracey_parser_check(parser, TRACEY_TOKEN_IDENTIFIER))
        {
            if (fn_sig) tracey_func_sig_free(fn_sig);
            tracey_parser_error(parser, "Expected variable name", &parser->current);
            return NULL;
        }
        name_tok = parser->current;
    }

    vd = tracey_stmt_create(TRACEY_STMT_VAR_DECL,
        (struct tracey_source_loc){name_tok.line, name_tok.column});
    if (!vd) { tracey_func_sig_free(fn_sig); return NULL; }
    vd->as.var_decl_stmt.type = t;
    vd->as.var_decl_stmt.struct_id = struct_id;
    vd->as.var_decl_stmt.fn_sig = fn_sig;
    vd->as.var_decl_stmt.name = tracey_token_strdup(&name_tok);
    if (!have_name)
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
        if (tracey_parser_check(parser, TRACEY_TOKEN_LBRACE))
        {
            struct tracey_expr *il = tracey_expr_create(TRACEY_EXPR_INIT_LIST,
                (struct tracey_source_loc){parser->current.line, parser->current.column});
            tracey_parser_advance(parser); /* '{' */
            if (il == NULL) { tracey_stmt_destroy(vd); return NULL; }
            if (!tracey_parser_check(parser, TRACEY_TOKEN_RBRACE))
            {
                for (;;)
                {
                    struct tracey_expr *it = tracey_parse_expression(parser);
                    struct tracey_expr **ni;
                    if (it == NULL) { tracey_expr_destroy(il); tracey_stmt_destroy(vd); return NULL; }
                    ni = (struct tracey_expr **)realloc(il->as.init_list_expr.items,
                                                       (il->as.init_list_expr.count + 1) * sizeof(*ni));
                    if (ni == NULL) { tracey_expr_destroy(it); tracey_expr_destroy(il); tracey_stmt_destroy(vd); return NULL; }
                    il->as.init_list_expr.items = ni;
                    il->as.init_list_expr.items[il->as.init_list_expr.count++] = it;
                    if (!tracey_parser_match(parser, TRACEY_TOKEN_COMMA))
                        break;
                    if (tracey_parser_check(parser, TRACEY_TOKEN_RBRACE))
                        break;
                }
            }
            if (!tracey_parser_expect(parser, TRACEY_TOKEN_RBRACE, "expected '}'"))
            { tracey_expr_destroy(il); tracey_stmt_destroy(vd); return NULL; }
            vd->as.var_decl_stmt.init = il;
        }
        else
        {
            vd->as.var_decl_stmt.init = tracey_parse_expression(parser);
            if (!vd->as.var_decl_stmt.init) { tracey_stmt_destroy(vd); return NULL; }
        }
    }
    return vd;
}


struct tracey_stmt *tracey_parse_for(struct tracey_parser *parser)
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


struct tracey_stmt *tracey_parse_break(struct tracey_parser *parser)
{
    struct tracey_source_loc loc = {parser->current.line, parser->current.column};
    struct tracey_stmt *s = tracey_stmt_create(TRACEY_STMT_BREAK, loc);
    if (!s) return NULL;
    tracey_parser_advance(parser);
    if (!tracey_parser_expect(parser, TRACEY_TOKEN_SEMICOLON, "expected ';'")) { tracey_stmt_destroy(s); return NULL; }
    return s;
}


struct tracey_stmt *tracey_parse_continue(struct tracey_parser *parser)
{
    struct tracey_source_loc loc = {parser->current.line, parser->current.column};
    struct tracey_stmt *s = tracey_stmt_create(TRACEY_STMT_CONTINUE, loc);
    if (!s) return NULL;
    tracey_parser_advance(parser);
    if (!tracey_parser_expect(parser, TRACEY_TOKEN_SEMICOLON, "expected ';'")) { tracey_stmt_destroy(s); return NULL; }
    return s;
}


struct tracey_stmt *tracey_parse_goto(struct tracey_parser *parser)
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


struct tracey_stmt *tracey_parse_return(struct tracey_parser *parser)
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


struct tracey_stmt *tracey_parse_statement(struct tracey_parser *parser)
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
        return tracey_parse_var_decl_as_stmt(parser);
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


