#include "semantic_internal.h"


void tracey_semantic_check_stmt(struct tracey_semantic_analyzer *sa, struct tracey_stmt *s,
                                       const struct tracey_function_decl *fn,
                                       enum tracey_type_kind expected_return)
{
    size_t i;
    if (s == NULL)
        return;

    switch (s->kind)
    {
        case TRACEY_STMT_BLOCK:
            tracey_semantic_push_scope(sa);
            for (i = 0; i < s->as.block_stmt.count; i++)
                tracey_semantic_check_stmt(sa, s->as.block_stmt.statements[i], fn, expected_return);
            tracey_semantic_pop_scope(sa);
            return;
        case TRACEY_STMT_VAR_DECL:
        {
            enum tracey_type_kind vt = s->as.var_decl_stmt.type;
            struct tracey_semantic_symbol sym;
            if (vt == TRACEY_TYPE_VOID)
            {
                tracey_semantic_error(sa, "Variable cannot have type void", s->loc);
                return;
            }
            if (s->as.var_decl_stmt.array_size > 0)
            {
                if (vt == TRACEY_TYPE_INT) vt = TRACEY_TYPE_INT_PTR;
                else if (vt == TRACEY_TYPE_UNSIGNED_INT) vt = TRACEY_TYPE_INT_PTR;
                else if (vt == TRACEY_TYPE_CHAR) vt = TRACEY_TYPE_CHAR_PTR;
                else if (vt == TRACEY_TYPE_SHORT) vt = TRACEY_TYPE_SHORT_PTR;
                else if (vt == TRACEY_TYPE_UNSIGNED_SHORT) vt = TRACEY_TYPE_SHORT_PTR;
                else if (vt == TRACEY_TYPE_LONG) vt = TRACEY_TYPE_LONG_PTR;
                else if (vt == TRACEY_TYPE_UNSIGNED_LONG) vt = TRACEY_TYPE_LONG_PTR;
                else if (vt == TRACEY_TYPE_LONG_LONG) vt = TRACEY_TYPE_LONG_LONG_PTR;
                else if (vt == TRACEY_TYPE_UNSIGNED_LONG_LONG) vt = TRACEY_TYPE_LONG_LONG_PTR;
                else if (vt == TRACEY_TYPE_SIZE_T) vt = TRACEY_TYPE_SIZE_T_PTR;
                else if (vt == TRACEY_TYPE_FLOAT) vt = TRACEY_TYPE_FLOAT_PTR;
                else if (vt == TRACEY_TYPE_DOUBLE) vt = TRACEY_TYPE_DOUBLE_PTR;
            }
            sym.type = vt;
            sym.is_function = false;
            sym.arity = 0;
            sym.struct_id = s->as.var_decl_stmt.struct_id;
            sym.fn_sig = s->as.var_decl_stmt.fn_sig;
            if (vt == TRACEY_TYPE_STRUCT && s->as.var_decl_stmt.init != NULL &&
                s->as.var_decl_stmt.init->kind != TRACEY_EXPR_INIT_LIST &&
                tracey_semantic_check_expr(sa, s->as.var_decl_stmt.init, fn) != TRACEY_TYPE_STRUCT &&
                tracey_semantic_check_expr(sa, s->as.var_decl_stmt.init, fn) != TRACEY_TYPE_STRUCT_PTR)
            {
                tracey_semantic_error(sa, "Struct initialization is not supported yet", s->loc);
                return;
            }
            if (vt == TRACEY_TYPE_STRUCT && s->as.var_decl_stmt.init != NULL &&
                s->as.var_decl_stmt.init->kind != TRACEY_EXPR_INIT_LIST)
                (void)tracey_semantic_check_expr(sa, s->as.var_decl_stmt.init, fn);
            if (s->as.var_decl_stmt.init != NULL &&
                s->as.var_decl_stmt.init->kind == TRACEY_EXPR_INIT_LIST &&
                vt != TRACEY_TYPE_STRUCT)
            {
                tracey_semantic_error(sa, "Initializer list requires a struct type", s->loc);
                return;
            }
            if (vt == TRACEY_TYPE_STRUCT && s->as.var_decl_stmt.init != NULL &&
                s->as.var_decl_stmt.init->kind == TRACEY_EXPR_INIT_LIST)
            {
                struct tracey_init_list_expr *il = &s->as.var_decl_stmt.init->as.init_list_expr;
                const struct tracey_struct_def *def = NULL;
                size_t i;

                if (s->as.var_decl_stmt.struct_id >= 0 && sa->program != NULL &&
                    (size_t)s->as.var_decl_stmt.struct_id < sa->program->struct_count)
                    def = &sa->program->structs[s->as.var_decl_stmt.struct_id];

                if (def != NULL && il->count > def->member_count)
                {
                    tracey_semantic_error(sa, "Too many initializers for struct", s->loc);
                    return;
                }
                for (i = 0; i < il->count; i++)
                {
                    enum tracey_type_kind it = tracey_semantic_check_expr(sa, il->items[i], fn);

                    if (def != NULL && i < def->member_count &&
                        !tracey_types_assignable(def->members[i].type, it) &&
                        !tracey_semantic_is_narrowing_conversion(def->members[i].type, it))
                        tracey_semantic_error(sa, "Type mismatch in struct initializer", il->items[i]->loc);
                    else if (def != NULL && i < def->member_count &&
                             tracey_semantic_is_narrowing_conversion(def->members[i].type, it))
                        tracey_semantic_warning(sa, "Narrowing conversion in initializer", il->items[i]->loc);
                }
            }
            if (!tracey_semantic_declare(sa, s->as.var_decl_stmt.name, sym, s->loc))
                return;
            if (s->as.var_decl_stmt.init &&
                s->as.var_decl_stmt.init->kind != TRACEY_EXPR_INIT_LIST)
            {
                enum tracey_type_kind t = tracey_semantic_check_expr(sa, s->as.var_decl_stmt.init, fn);
                if (!tracey_types_assignable(vt, t) &&
                    !tracey_semantic_is_narrowing_conversion(vt, t))
                    tracey_semantic_error(sa, "Type mismatch in initializer", s->loc);
                else if (tracey_semantic_is_narrowing_conversion(vt, t))
                    tracey_semantic_warning(sa, "Narrowing conversion in initializer", s->loc);
            }
            else if (s->as.var_decl_stmt.init &&
                     s->as.var_decl_stmt.init->kind == TRACEY_EXPR_INIT_LIST)
            {
                (void)tracey_semantic_check_expr(sa, s->as.var_decl_stmt.init, fn);
            }
            return;
        }
        case TRACEY_STMT_EXPR:
            (void)tracey_semantic_check_expr(sa, s->as.expr_stmt.expression, fn);
            return;
        case TRACEY_STMT_IF:
        {
            enum tracey_type_kind t = tracey_semantic_check_expr(sa, s->as.if_stmt.condition, fn);
            if (t != TRACEY_TYPE_BOOL)
                tracey_semantic_error(sa, "If condition must be bool", s->loc);
            tracey_semantic_check_stmt(sa, s->as.if_stmt.then_branch, fn, expected_return);
            if (s->as.if_stmt.else_branch)
                tracey_semantic_check_stmt(sa, s->as.if_stmt.else_branch, fn, expected_return);
            return;
        }
        case TRACEY_STMT_DO_WHILE:
        {
            sa->loop_depth++;
            tracey_semantic_check_stmt(sa, s->as.do_while_stmt.body, fn, expected_return);
            {
                enum tracey_type_kind t = tracey_semantic_check_expr(sa, s->as.do_while_stmt.condition, fn);
                if (t != TRACEY_TYPE_BOOL)
                    tracey_semantic_error(sa, "Do-while condition must be bool", s->loc);
            }
            sa->loop_depth--;
            return;
        }
        case TRACEY_STMT_SWITCH:
        {
            enum tracey_type_kind t = tracey_semantic_check_expr(sa, s->as.switch_stmt.condition, fn);
            if (!tracey_is_integer(t))
                tracey_semantic_error(sa, "Switch condition must be integer", s->loc);
            sa->switch_depth++;
            for (size_t ci = 0; ci < s->as.switch_stmt.case_count; ci++)
            {
                /* uniqueness check */
                for (size_t cj = ci + 1; cj < s->as.switch_stmt.case_count; cj++)
                {
                    if (s->as.switch_stmt.cases[ci].value == s->as.switch_stmt.cases[cj].value)
                        tracey_semantic_error(sa, "Duplicate case value", s->loc);
                }
                if (s->as.switch_stmt.cases[ci].body)
                    tracey_semantic_check_stmt(sa, s->as.switch_stmt.cases[ci].body, fn, expected_return);
            }
            if (s->as.switch_stmt.default_body)
                tracey_semantic_check_stmt(sa, s->as.switch_stmt.default_body, fn, expected_return);
            sa->switch_depth--;
            return;
        }
        case TRACEY_STMT_WHILE:
        {
            sa->loop_depth++;
            enum tracey_type_kind t = tracey_semantic_check_expr(sa, s->as.while_stmt.condition, fn);
            if (t != TRACEY_TYPE_BOOL)
                tracey_semantic_error(sa, "While condition must be bool", s->loc);
            tracey_semantic_check_stmt(sa, s->as.while_stmt.body, fn, expected_return);
            sa->loop_depth--;
            return;
        }
        case TRACEY_STMT_RETURN:
            if (s->as.return_stmt.value)
            {
                enum tracey_type_kind t = tracey_semantic_check_expr(sa, s->as.return_stmt.value, fn);
                if (t != expected_return)
                    tracey_semantic_error(sa, "Return type mismatch", s->loc);
            }
            else
            {
                if (expected_return != TRACEY_TYPE_VOID)
                    tracey_semantic_error(sa, "Expected return value", s->loc);
            }
            return;
        case TRACEY_STMT_FOR:
        {
            sa->loop_depth++;
            if (s->as.for_stmt.init)
                tracey_semantic_check_stmt(sa, s->as.for_stmt.init, fn, expected_return);
            if (s->as.for_stmt.condition)
            {
                enum tracey_type_kind t = tracey_semantic_check_expr(sa, s->as.for_stmt.condition, fn);
                if (t != TRACEY_TYPE_BOOL)
                    tracey_semantic_error(sa, "For condition must be bool", s->loc);
            }
            if (s->as.for_stmt.update)
                (void)tracey_semantic_check_expr(sa, s->as.for_stmt.update, fn);
            tracey_semantic_check_stmt(sa, s->as.for_stmt.body, fn, expected_return);
            sa->loop_depth--;
            return;
        }
        case TRACEY_STMT_BREAK:
            if (sa->loop_depth <= 0 && sa->switch_depth <= 0)
                tracey_semantic_error(sa, "'break' must be inside a loop or switch", s->loc);
            return;
        case TRACEY_STMT_CONTINUE:
            if (sa->loop_depth <= 0)
                tracey_semantic_error(sa, "'continue' must be inside a loop", s->loc);
            return;
        case TRACEY_STMT_GOTO:
        {
            /* Forward-only goto within a function: record label usage. */
            if (s->as.goto_stmt.label != NULL)
            {
                size_t j;
                for (j = 0; j < sa->goto_label_count; j++)
                {
                    if (strcmp(sa->goto_labels[j], s->as.goto_stmt.label) == 0)
                        break;
                }
                if (j == sa->goto_label_count)
                {
                    if (sa->goto_label_count == sa->goto_label_cap)
                    {
                        size_t new_cap = sa->goto_label_cap ? sa->goto_label_cap * 2 : 8;
                        char **new_items = (char **)realloc(sa->goto_labels,
                            new_cap * sizeof(*new_items));
                        size_t *new_lines = (size_t *)realloc(sa->goto_label_lines,
                            new_cap * sizeof(*new_lines));
                        if (new_items == NULL || new_lines == NULL) return;
                        sa->goto_labels = new_items;
                        sa->goto_label_lines = new_lines;
                        sa->goto_label_cap = new_cap;
                    }
                    sa->goto_labels[sa->goto_label_count] = strdup(s->as.goto_stmt.label);
                    sa->goto_label_lines[sa->goto_label_count] = s->loc.line;
                    sa->goto_label_count++;
                }
            }
            return;
        }
        case TRACEY_STMT_LABEL:
        {
            if (s->as.label_stmt.label != NULL)
            {
                size_t j;
                for (j = 0; j < sa->declared_label_count; j++)
                {
                    if (strcmp(sa->declared_labels[j], s->as.label_stmt.label) == 0)
                        break;
                }
                if (j == sa->declared_label_count)
                {
                    if (sa->declared_label_count == sa->declared_label_cap)
                    {
                        size_t new_cap = sa->declared_label_cap ? sa->declared_label_cap * 2 : 8;
                        char **new_items = (char **)realloc(sa->declared_labels,
                            new_cap * sizeof(*new_items));
                        size_t *new_lines = (size_t *)realloc(sa->declared_label_lines,
                            new_cap * sizeof(*new_lines));
                        if (new_items == NULL || new_lines == NULL) { tracey_semantic_check_stmt(sa, s->as.label_stmt.stmt, fn, expected_return); return; }
                        sa->declared_labels = new_items;
                        sa->declared_label_lines = new_lines;
                        sa->declared_label_cap = new_cap;
                    }
                    sa->declared_labels[sa->declared_label_count] = strdup(s->as.label_stmt.label);
                    sa->declared_label_lines[sa->declared_label_count] = s->loc.line;
                    sa->declared_label_count++;
                }
            }
            tracey_semantic_check_stmt(sa, s->as.label_stmt.stmt, fn, expected_return);
            return;
        }
        default:
            return;
    }
}


void tracey_semantic_check_function(struct tracey_semantic_analyzer *sa,
                                           struct tracey_function_decl *fn)
{
    size_t i;
    tracey_semantic_push_scope(sa);
    for (i = 0; i < fn->param_count; i++)
    {
        struct tracey_semantic_symbol sym;
        if (fn->params[i].type == TRACEY_TYPE_VOID)
            tracey_semantic_error(sa, "Parameter cannot have type void", fn->loc);
        sym.type = fn->params[i].type;
        sym.is_function = false;
        sym.arity = 0;
        sym.struct_id = fn->params[i].struct_id;
        sym.fn_sig = fn->params[i].fn_sig;
        tracey_semantic_declare(sa, fn->params[i].name, sym, fn->loc);
    }
    if (fn->body)
    {
        /* Reset label tracking per function. */
        tracey_semantic_string_array_free(sa->goto_labels, sa->goto_label_count);
        sa->goto_labels = NULL;
        sa->goto_label_count = 0;
        sa->goto_label_cap = 0;
        tracey_semantic_string_array_free(sa->declared_labels, sa->declared_label_count);
        sa->declared_labels = NULL;
        sa->declared_label_count = 0;
        sa->declared_label_cap = 0;

        tracey_semantic_check_stmt(sa, fn->body, fn, fn->return_type);

        /* Validate: every goto label must be declared, every declared label must be used. */
        for (i = 0; i < sa->goto_label_count; i++)
        {
            size_t j;
            bool found = false;
            for (j = 0; j < sa->declared_label_count; j++)
            {
                if (strcmp(sa->goto_labels[i], sa->declared_labels[j]) == 0)
                {
                    found = true;
                    if (sa->declared_label_lines != NULL && sa->goto_label_lines != NULL &&
                        sa->declared_label_lines[j] < sa->goto_label_lines[i])
                    {
                        char buf[256];
                        snprintf(buf, sizeof(buf), "Backward goto to label '%s' is not allowed",
                                 sa->goto_labels[i]);
                        tracey_semantic_error(sa, buf, fn->loc);
                    }
                    break;
                }
            }
            if (!found)
            {
                char buf[256];
                snprintf(buf, sizeof(buf), "Goto label '%s' is not declared", sa->goto_labels[i]);
                tracey_semantic_error(sa, buf, fn->loc);
            }
        }
        for (i = 0; i < sa->declared_label_count; i++)
        {
            size_t j;
            bool used = false;
            for (j = 0; j < sa->goto_label_count; j++)
            {
                if (strcmp(sa->declared_labels[i], sa->goto_labels[j]) == 0)
                {
                    used = true;
                    break;
                }
            }
            if (!used)
            {
                char buf[256];
                snprintf(buf, sizeof(buf), "Label '%s' is declared but not used", sa->declared_labels[i]);
                tracey_semantic_error(sa, buf, fn->loc);
            }
        }
    }
    tracey_semantic_pop_scope(sa);
}

