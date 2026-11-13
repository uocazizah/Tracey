#include "tracey/semantic.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char *tracey_strdup_local(const char *s)
{
    char *copy;
    size_t len;
    if (s == NULL)
        s = "";
    len = strlen(s);
    copy = (char *)malloc(len + 1);
    if (copy == NULL)
        return NULL;
    memcpy(copy, s, len + 1);
    return copy;
}

void tracey_semantic_init(struct tracey_semantic_analyzer *sa)
{
    if (sa == NULL)
        return;
    memset(sa, 0, sizeof(*sa));
}

static void tracey_semantic_scope_free(struct tracey_semantic_scope *scope)
{
    size_t i;
    if (scope == NULL)
        return;
    for (i = 0; i < scope->count; i++)
        free(scope->bindings[i].name);
    free(scope->bindings);
    scope->bindings = NULL;
    scope->count = 0;
    scope->cap = 0;
}

void tracey_semantic_free(struct tracey_semantic_analyzer *sa)
{
    size_t i;
    if (sa == NULL)
        return;
    for (i = 0; i < sa->error_count; i++)
        free(sa->errors[i].message);
    free(sa->errors);
    for (i = 0; i < sa->scope_count; i++)
        tracey_semantic_scope_free(&sa->scopes[i]);
    free(sa->scopes);
    tracey_semantic_init(sa);
}

static void tracey_semantic_error(struct tracey_semantic_analyzer *sa, const char *msg,
                                  struct tracey_source_loc loc)
{
    struct tracey_semantic_error *new_errors;
    char *copy;
    if (sa == NULL)
        return;
    sa->had_error = true;
    if (sa->error_count == sa->error_cap)
    {
        size_t new_cap = sa->error_cap ? sa->error_cap * 2 : 8;
        new_errors = (struct tracey_semantic_error *)realloc(sa->errors,
                                                             new_cap * sizeof(*new_errors));
        if (new_errors == NULL)
            return;
        sa->errors = new_errors;
        sa->error_cap = new_cap;
    }
    copy = tracey_strdup_local(msg);
    if (copy == NULL)
        return;
    sa->errors[sa->error_count].message = copy;
    sa->errors[sa->error_count].line = loc.line;
    sa->errors[sa->error_count].column = loc.column;
    sa->error_count++;
}

static void tracey_semantic_push_scope(struct tracey_semantic_analyzer *sa)
{
    struct tracey_semantic_scope *new_scopes;
    if (sa->scope_count == sa->scope_cap)
    {
        size_t new_cap = sa->scope_cap ? sa->scope_cap * 2 : 8;
        new_scopes = (struct tracey_semantic_scope *)realloc(sa->scopes,
                                                             new_cap * sizeof(*new_scopes));
        if (new_scopes == NULL)
            return;
        sa->scopes = new_scopes;
        sa->scope_cap = new_cap;
    }
    memset(&sa->scopes[sa->scope_count], 0, sizeof(sa->scopes[sa->scope_count]));
    sa->scope_count++;
}

static void tracey_semantic_pop_scope(struct tracey_semantic_analyzer *sa)
{
    if (sa->scope_count == 0)
        return;
    sa->scope_count--;
    tracey_semantic_scope_free(&sa->scopes[sa->scope_count]);
}

static bool tracey_semantic_declare(struct tracey_semantic_analyzer *sa, const char *name,
                                    struct tracey_semantic_symbol sym, struct tracey_source_loc loc)
{
    struct tracey_semantic_scope *scope;
    size_t i;
    struct tracey_semantic_binding *new_bindings;
    char *copy;

    if (sa->scope_count == 0)
        tracey_semantic_push_scope(sa);
    if (sa->scope_count == 0)
        return false;
    scope = &sa->scopes[sa->scope_count - 1];

    for (i = 0; i < scope->count; i++)
    {
        if (strcmp(scope->bindings[i].name, name) == 0)
        {
            struct tracey_semantic_symbol old = scope->bindings[i].symbol;
            if (old.is_function && sym.is_function && old.type == sym.type && old.arity == sym.arity)
                return true;
            {
                char buf[256];
                snprintf(buf, sizeof(buf), "Redeclaration of '%s'", name);
                tracey_semantic_error(sa, buf, loc);
            }
            return false;
        }
    }

    if (scope->count == scope->cap)
    {
        size_t new_cap = scope->cap ? scope->cap * 2 : 8;
        new_bindings = (struct tracey_semantic_binding *)realloc(scope->bindings,
                                                                 new_cap * sizeof(*new_bindings));
        if (new_bindings == NULL)
            return false;
        scope->bindings = new_bindings;
        scope->cap = new_cap;
    }

    copy = tracey_strdup_local(name);
    if (copy == NULL)
        return false;
    scope->bindings[scope->count].name = copy;
    scope->bindings[scope->count].symbol = sym;
    scope->count++;
    return true;
}

static bool tracey_semantic_lookup(const struct tracey_semantic_analyzer *sa, const char *name,
                                   struct tracey_semantic_symbol *out)
{
    size_t si;
    if (sa == NULL || name == NULL)
        return false;
    for (si = sa->scope_count; si > 0; si--)
    {
        const struct tracey_semantic_scope *scope = &sa->scopes[si - 1];
        size_t i;
        for (i = 0; i < scope->count; i++)
        {
            if (strcmp(scope->bindings[i].name, name) == 0)
            {
                if (out)
                    *out = scope->bindings[i].symbol;
                return true;
            }
        }
    }
    return false;
}

static enum tracey_type_kind tracey_semantic_check_expr(struct tracey_semantic_analyzer *sa,
                                                        struct tracey_expr *e,
                                                        const struct tracey_function_decl *fn);

static void tracey_semantic_check_stmt(struct tracey_semantic_analyzer *sa, struct tracey_stmt *s,
                                       const struct tracey_function_decl *fn,
                                       enum tracey_type_kind expected_return);

static bool tracey_is_numeric(enum tracey_type_kind t)
{
    return t == TRACEY_TYPE_INT || t == TRACEY_TYPE_FLOAT || t == TRACEY_TYPE_DOUBLE;
}

static enum tracey_type_kind tracey_semantic_check_expr(struct tracey_semantic_analyzer *sa,
                                                        struct tracey_expr *e,
                                                        const struct tracey_function_decl *fn)
{
    (void)fn;
    if (e == NULL)
        return TRACEY_TYPE_VOID;

    switch (e->kind)
    {
        case TRACEY_EXPR_INT:
            return TRACEY_TYPE_INT;
        case TRACEY_EXPR_BOOL:
            return TRACEY_TYPE_BOOL;
        case TRACEY_EXPR_REAL:
            return e->as.real_expr.is_float ? TRACEY_TYPE_FLOAT : TRACEY_TYPE_DOUBLE;
        case TRACEY_EXPR_STRING:
            return TRACEY_TYPE_CHAR_PTR;
        case TRACEY_EXPR_IDENT:
        {
            struct tracey_semantic_symbol sym;
            if (!tracey_semantic_lookup(sa, e->as.ident_expr.name, &sym))
            {
                char buf[256];
                snprintf(buf, sizeof(buf), "Undefined identifier '%s'", e->as.ident_expr.name);
                tracey_semantic_error(sa, buf, e->loc);
                return TRACEY_TYPE_INT;
            }
            if (sym.is_function)
            {
                char buf[256];
                snprintf(buf, sizeof(buf), "'%s' is a function", e->as.ident_expr.name);
                tracey_semantic_error(sa, buf, e->loc);
                return TRACEY_TYPE_INT;
            }
            return sym.type;
        }
        case TRACEY_EXPR_INDEX:
        {
            enum tracey_type_kind bt = tracey_semantic_check_expr(sa, e->as.index_expr.base, fn);
            enum tracey_type_kind it = tracey_semantic_check_expr(sa, e->as.index_expr.index, fn);
            if (it != TRACEY_TYPE_INT)
                tracey_semantic_error(sa, "Array index must be int", e->loc);
            if (bt == TRACEY_TYPE_INT_PTR) return TRACEY_TYPE_INT;
            if (bt == TRACEY_TYPE_CHAR_PTR) return TRACEY_TYPE_CHAR;
            if (bt == TRACEY_TYPE_FLOAT_PTR) return TRACEY_TYPE_FLOAT;
            if (bt == TRACEY_TYPE_DOUBLE_PTR) return TRACEY_TYPE_DOUBLE;
            tracey_semantic_error(sa, "Subscript requires array or pointer", e->loc);
            return TRACEY_TYPE_INT;
        }
        case TRACEY_EXPR_ADDR:
        {
            enum tracey_type_kind t = tracey_semantic_check_expr(sa, e->as.addr_expr.operand, fn);
            if (t == TRACEY_TYPE_INT) return TRACEY_TYPE_INT_PTR;
            if (t == TRACEY_TYPE_CHAR) return TRACEY_TYPE_CHAR_PTR;
            if (t == TRACEY_TYPE_FLOAT) return TRACEY_TYPE_FLOAT_PTR;
            if (t == TRACEY_TYPE_DOUBLE) return TRACEY_TYPE_DOUBLE_PTR;
            tracey_semantic_error(sa, "Cannot take address", e->loc);
            return TRACEY_TYPE_INT_PTR;
        }
        case TRACEY_EXPR_DEREF:
        {
            enum tracey_type_kind t = tracey_semantic_check_expr(sa, e->as.deref_expr.operand, fn);
            if (t == TRACEY_TYPE_INT_PTR) return TRACEY_TYPE_INT;
            if (t == TRACEY_TYPE_CHAR_PTR) return TRACEY_TYPE_CHAR;
            if (t == TRACEY_TYPE_FLOAT_PTR) return TRACEY_TYPE_FLOAT;
            if (t == TRACEY_TYPE_DOUBLE_PTR) return TRACEY_TYPE_DOUBLE;
            tracey_semantic_error(sa, "Cannot dereference non-pointer", e->loc);
            return TRACEY_TYPE_INT;
        }
        case TRACEY_EXPR_UNARY:
        {
            enum tracey_type_kind t = tracey_semantic_check_expr(sa, e->as.unary_expr.operand, fn);
            if (e->as.unary_expr.op == TRACEY_UNARY_NEG)
            {
                if (t == TRACEY_TYPE_INT) return TRACEY_TYPE_INT;
                if (t == TRACEY_TYPE_FLOAT) return TRACEY_TYPE_FLOAT;
                if (t == TRACEY_TYPE_DOUBLE) return TRACEY_TYPE_DOUBLE;
                tracey_semantic_error(sa, "Unary '-' requires numeric", e->loc);
                return TRACEY_TYPE_INT;
            }
            if (t != TRACEY_TYPE_BOOL)
                tracey_semantic_error(sa, "'!' requires bool", e->loc);
            return TRACEY_TYPE_BOOL;
        }
        case TRACEY_EXPR_BINARY:
        {
            if (e->as.binary_expr.op == TRACEY_BINARY_ASSIGN)
            {
                struct tracey_expr *lhs = e->as.binary_expr.left;
                if (lhs && lhs->kind == TRACEY_EXPR_IDENT)
                {
                    struct tracey_semantic_symbol sym;
                    if (!tracey_semantic_lookup(sa, lhs->as.ident_expr.name, &sym) || sym.is_function)
                    {
                        char buf[256];
                        snprintf(buf, sizeof(buf), "Undefined variable '%s'", lhs->as.ident_expr.name);
                        tracey_semantic_error(sa, buf, e->loc);
                        (void)tracey_semantic_check_expr(sa, e->as.binary_expr.right, fn);
                        return TRACEY_TYPE_INT;
                    }
                    {
                        enum tracey_type_kind rt = tracey_semantic_check_expr(sa, e->as.binary_expr.right, fn);
                        if (rt != sym.type)
                            tracey_semantic_error(sa, "Type mismatch in assignment", e->loc);
                        return sym.type;
                    }
                }
                if (lhs && lhs->kind == TRACEY_EXPR_INDEX)
                {
                    enum tracey_type_kind bt = tracey_semantic_check_expr(sa, lhs->as.index_expr.base, fn);
                    enum tracey_type_kind it = tracey_semantic_check_expr(sa, lhs->as.index_expr.index, fn);
                    enum tracey_type_kind elem = TRACEY_TYPE_INT;
                    if (it != TRACEY_TYPE_INT)
                        tracey_semantic_error(sa, "Array index must be int", lhs->loc);
                    if (bt == TRACEY_TYPE_CHAR_PTR) elem = TRACEY_TYPE_CHAR;
                    else if (bt == TRACEY_TYPE_FLOAT_PTR) elem = TRACEY_TYPE_FLOAT;
                    else if (bt == TRACEY_TYPE_DOUBLE_PTR) elem = TRACEY_TYPE_DOUBLE;
                    else if (bt != TRACEY_TYPE_INT_PTR)
                        tracey_semantic_error(sa, "Subscript requires array or pointer", lhs->loc);
                    {
                        enum tracey_type_kind rt = tracey_semantic_check_expr(sa, e->as.binary_expr.right, fn);
                        if (rt != elem)
                            tracey_semantic_error(sa, "Type mismatch in assignment", e->loc);
                    }
                    return elem;
                }
                if (lhs && lhs->kind == TRACEY_EXPR_DEREF)
                {
                    enum tracey_type_kind pt = tracey_semantic_check_expr(sa, lhs->as.deref_expr.operand, fn);
                    enum tracey_type_kind elem = TRACEY_TYPE_INT;
                    if (pt == TRACEY_TYPE_CHAR_PTR) elem = TRACEY_TYPE_CHAR;
                    else if (pt == TRACEY_TYPE_FLOAT_PTR) elem = TRACEY_TYPE_FLOAT;
                    else if (pt == TRACEY_TYPE_DOUBLE_PTR) elem = TRACEY_TYPE_DOUBLE;
                    else if (pt != TRACEY_TYPE_INT_PTR)
                        tracey_semantic_error(sa, "Cannot dereference non-pointer", lhs->loc);
                    {
                        enum tracey_type_kind rt = tracey_semantic_check_expr(sa, e->as.binary_expr.right, fn);
                        if (rt != elem)
                            tracey_semantic_error(sa, "Type mismatch in assignment", lhs->loc);
                    }
                    return elem;
                }
                tracey_semantic_error(sa, "Invalid assignment target", e->loc);
                (void)tracey_semantic_check_expr(sa, e->as.binary_expr.right, fn);
                return TRACEY_TYPE_INT;
            }
            {
                enum tracey_type_kind lt = tracey_semantic_check_expr(sa, e->as.binary_expr.left, fn);
                enum tracey_type_kind rt = tracey_semantic_check_expr(sa, e->as.binary_expr.right, fn);
                switch (e->as.binary_expr.op)
                {
                    case TRACEY_BINARY_ADD:
                    case TRACEY_BINARY_SUB:
                    case TRACEY_BINARY_MUL:
                    case TRACEY_BINARY_DIV:
                        if (lt == TRACEY_TYPE_DOUBLE || rt == TRACEY_TYPE_DOUBLE)
                        {
                            if ((lt == TRACEY_TYPE_DOUBLE || lt == TRACEY_TYPE_INT || lt == TRACEY_TYPE_FLOAT) &&
                                (rt == TRACEY_TYPE_DOUBLE || rt == TRACEY_TYPE_INT || rt == TRACEY_TYPE_FLOAT))
                                return TRACEY_TYPE_DOUBLE;
                            tracey_semantic_error(sa, "Arithmetic requires numeric operands", e->loc);
                            return TRACEY_TYPE_DOUBLE;
                        }
                        if (lt == TRACEY_TYPE_FLOAT || rt == TRACEY_TYPE_FLOAT)
                        {
                            if ((lt == TRACEY_TYPE_FLOAT || lt == TRACEY_TYPE_INT) &&
                                (rt == TRACEY_TYPE_FLOAT || rt == TRACEY_TYPE_INT))
                                return TRACEY_TYPE_FLOAT;
                            tracey_semantic_error(sa, "Arithmetic requires numeric operands", e->loc);
                            return TRACEY_TYPE_FLOAT;
                        }
                        if (lt != TRACEY_TYPE_INT || rt != TRACEY_TYPE_INT)
                            tracey_semantic_error(sa, "Arithmetic requires int operands", e->loc);
                        return TRACEY_TYPE_INT;
                    case TRACEY_BINARY_MOD:
                        if (lt != TRACEY_TYPE_INT || rt != TRACEY_TYPE_INT)
                            tracey_semantic_error(sa, "Modulo requires int operands", e->loc);
                        return TRACEY_TYPE_INT;
                    case TRACEY_BINARY_EQ:
                    case TRACEY_BINARY_NE:
                        if (lt != rt)
                            tracey_semantic_error(sa, "Comparison requires same types", e->loc);
                        return TRACEY_TYPE_BOOL;
                    case TRACEY_BINARY_LT:
                    case TRACEY_BINARY_GT:
                    case TRACEY_BINARY_LE:
                    case TRACEY_BINARY_GE:
                        if (tracey_is_numeric(lt) && tracey_is_numeric(rt))
                            return TRACEY_TYPE_BOOL;
                        tracey_semantic_error(sa, "Relational requires numeric operands", e->loc);
                        return TRACEY_TYPE_BOOL;
                    case TRACEY_BINARY_LOG_AND:
                    case TRACEY_BINARY_LOG_OR:
                        if (lt != TRACEY_TYPE_BOOL || rt != TRACEY_TYPE_BOOL)
                            tracey_semantic_error(sa, "Logical requires bool operands", e->loc);
                        return TRACEY_TYPE_BOOL;
                    default:
                        return TRACEY_TYPE_INT;
                }
            }
        }
        case TRACEY_EXPR_CALL:
        {
            const char *name = e->as.call_expr.callee;
            size_t i;
            if (name && strcmp(name, "print") == 0)
            {
                for (i = 0; i < e->as.call_expr.arg_count; i++)
                    tracey_semantic_check_expr(sa, e->as.call_expr.args[i], fn);
                return TRACEY_TYPE_VOID;
            }
            if (name && strcmp(name, "input_int") == 0)
            {
                if (e->as.call_expr.arg_count != 0)
                    tracey_semantic_error(sa, "Wrong number of arguments to 'input_int'", e->loc);
                return TRACEY_TYPE_INT;
            }
            if (name && strcmp(name, "input_str") == 0)
            {
                if (e->as.call_expr.arg_count != 2)
                    tracey_semantic_error(sa, "Wrong number of arguments to 'input_str'", e->loc);
                if (e->as.call_expr.arg_count >= 1)
                {
                    enum tracey_type_kind t = tracey_semantic_check_expr(sa, e->as.call_expr.args[0], fn);
                    if (t != TRACEY_TYPE_CHAR_PTR)
                        tracey_semantic_error(sa, "Input_str requires char* buffer", e->as.call_expr.args[0]->loc);
                }
                if (e->as.call_expr.arg_count >= 2)
                {
                    enum tracey_type_kind t = tracey_semantic_check_expr(sa, e->as.call_expr.args[1], fn);
                    if (t != TRACEY_TYPE_INT)
                        tracey_semantic_error(sa, "Input_str size must be int", e->as.call_expr.args[1]->loc);
                }
                return TRACEY_TYPE_INT;
            }
            if (name && strcmp(name, "printf") == 0)
            {
                for (i = 0; i < e->as.call_expr.arg_count; i++)
                    tracey_semantic_check_expr(sa, e->as.call_expr.args[i], fn);
                return TRACEY_TYPE_INT;
            }
            {
                struct tracey_semantic_symbol sym;
                if (!tracey_semantic_lookup(sa, name, &sym) || !sym.is_function)
                {
                    char buf[256];
                    snprintf(buf, sizeof(buf), "Undefined function '%s'", name ? name : "");
                    tracey_semantic_error(sa, buf, e->loc);
                    for (i = 0; i < e->as.call_expr.arg_count; i++)
                        tracey_semantic_check_expr(sa, e->as.call_expr.args[i], fn);
                    return TRACEY_TYPE_INT;
                }
                if ((int)e->as.call_expr.arg_count != sym.arity)
                {
                    char buf[256];
                    snprintf(buf, sizeof(buf), "Wrong number of arguments to '%s'", name);
                    tracey_semantic_error(sa, buf, e->loc);
                }
                for (i = 0; i < e->as.call_expr.arg_count; i++)
                    tracey_semantic_check_expr(sa, e->as.call_expr.args[i], fn);
                return sym.type;
            }
        }
        default:
            return TRACEY_TYPE_VOID;
    }
}

static void tracey_semantic_check_stmt(struct tracey_semantic_analyzer *sa, struct tracey_stmt *s,
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
                else if (vt == TRACEY_TYPE_CHAR) vt = TRACEY_TYPE_CHAR_PTR;
                else if (vt == TRACEY_TYPE_FLOAT) vt = TRACEY_TYPE_FLOAT_PTR;
                else if (vt == TRACEY_TYPE_DOUBLE) vt = TRACEY_TYPE_DOUBLE_PTR;
            }
            sym.type = vt;
            sym.is_function = false;
            sym.arity = 0;
            if (!tracey_semantic_declare(sa, s->as.var_decl_stmt.name, sym, s->loc))
                return;
            if (s->as.var_decl_stmt.init)
            {
                enum tracey_type_kind t = tracey_semantic_check_expr(sa, s->as.var_decl_stmt.init, fn);
                if (t != vt)
                    tracey_semantic_error(sa, "Type mismatch in initializer", s->loc);
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
        case TRACEY_STMT_WHILE:
        {
            enum tracey_type_kind t = tracey_semantic_check_expr(sa, s->as.while_stmt.condition, fn);
            if (t != TRACEY_TYPE_BOOL)
                tracey_semantic_error(sa, "While condition must be bool", s->loc);
            tracey_semantic_check_stmt(sa, s->as.while_stmt.body, fn, expected_return);
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
        default:
            return;
    }
}

static void tracey_semantic_check_function(struct tracey_semantic_analyzer *sa,
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
        tracey_semantic_declare(sa, fn->params[i].name, sym, fn->loc);
    }
    if (fn->body)
        tracey_semantic_check_stmt(sa, fn->body, fn, fn->return_type);
    tracey_semantic_pop_scope(sa);
}

bool tracey_semantic_analyze(struct tracey_semantic_analyzer *sa, struct tracey_program *program)
{
    size_t i;
    if (sa == NULL || program == NULL)
        return false;
    tracey_semantic_free(sa);
    tracey_semantic_init(sa);
    tracey_semantic_push_scope(sa);

    for (i = 0; i < program->function_count; i++)
    {
        struct tracey_function_decl *fn = program->functions[i];
        struct tracey_semantic_symbol sym;
        sym.type = fn->return_type;
        sym.is_function = true;
        sym.arity = (int)fn->param_count;
        tracey_semantic_declare(sa, fn->name, sym, fn->loc);
    }
    for (i = 0; i < program->function_count; i++)
        tracey_semantic_check_function(sa, program->functions[i]);

    tracey_semantic_pop_scope(sa);
    return !sa->had_error;
}
