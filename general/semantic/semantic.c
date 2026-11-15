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

static void tracey_semantic_string_array_free(char **items, size_t count)
{
    size_t i;
    if (items == NULL)
        return;
    for (i = 0; i < count; i++)
        free(items[i]);
    free(items);
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
    tracey_semantic_string_array_free(sa->goto_labels, sa->goto_label_count);
    tracey_semantic_string_array_free(sa->declared_labels, sa->declared_label_count);
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
    return t == TRACEY_TYPE_INT || t == TRACEY_TYPE_UNSIGNED_INT ||
           t == TRACEY_TYPE_BOOL || t == TRACEY_TYPE_CHAR ||
           t == TRACEY_TYPE_SHORT || t == TRACEY_TYPE_UNSIGNED_SHORT ||
           t == TRACEY_TYPE_LONG || t == TRACEY_TYPE_UNSIGNED_LONG ||
           t == TRACEY_TYPE_LONG_LONG || t == TRACEY_TYPE_UNSIGNED_LONG_LONG ||
           t == TRACEY_TYPE_SIZE_T || t == TRACEY_TYPE_FLOAT || t == TRACEY_TYPE_DOUBLE;
}

static bool tracey_is_integer(enum tracey_type_kind t)
{
    return t == TRACEY_TYPE_INT || t == TRACEY_TYPE_UNSIGNED_INT ||
           t == TRACEY_TYPE_CHAR || t == TRACEY_TYPE_SHORT ||
           t == TRACEY_TYPE_UNSIGNED_SHORT || t == TRACEY_TYPE_LONG ||
           t == TRACEY_TYPE_UNSIGNED_LONG || t == TRACEY_TYPE_LONG_LONG ||
           t == TRACEY_TYPE_UNSIGNED_LONG_LONG || t == TRACEY_TYPE_SIZE_T;
}

static bool tracey_is_pointer(enum tracey_type_kind t)
{
    switch (t)
    {
        case TRACEY_TYPE_CHAR_PTR:
        case TRACEY_TYPE_INT_PTR:
        case TRACEY_TYPE_SHORT_PTR:
        case TRACEY_TYPE_UNSIGNED_SHORT_PTR:
        case TRACEY_TYPE_LONG_PTR:
        case TRACEY_TYPE_UNSIGNED_LONG_PTR:
        case TRACEY_TYPE_LONG_LONG_PTR:
        case TRACEY_TYPE_UNSIGNED_LONG_LONG_PTR:
        case TRACEY_TYPE_SIZE_T_PTR:
        case TRACEY_TYPE_FLOAT_PTR:
        case TRACEY_TYPE_DOUBLE_PTR:
            return true;
        default:
            return false;
    }
}

static enum tracey_type_kind tracey_pointer_element_type(enum tracey_type_kind t)
{
    switch (t)
    {
        case TRACEY_TYPE_CHAR_PTR: return TRACEY_TYPE_CHAR;
        case TRACEY_TYPE_INT_PTR: return TRACEY_TYPE_INT;
        case TRACEY_TYPE_UNSIGNED_INT: return TRACEY_TYPE_UNSIGNED_INT;
        case TRACEY_TYPE_SHORT_PTR: return TRACEY_TYPE_SHORT;
        case TRACEY_TYPE_UNSIGNED_SHORT_PTR: return TRACEY_TYPE_UNSIGNED_SHORT;
        case TRACEY_TYPE_LONG_PTR: return TRACEY_TYPE_LONG;
        case TRACEY_TYPE_UNSIGNED_LONG_PTR: return TRACEY_TYPE_UNSIGNED_LONG;
        case TRACEY_TYPE_LONG_LONG_PTR: return TRACEY_TYPE_LONG_LONG;
        case TRACEY_TYPE_UNSIGNED_LONG_LONG_PTR: return TRACEY_TYPE_UNSIGNED_LONG_LONG;
        case TRACEY_TYPE_SIZE_T_PTR: return TRACEY_TYPE_SIZE_T;
        case TRACEY_TYPE_FLOAT_PTR: return TRACEY_TYPE_FLOAT;
        case TRACEY_TYPE_DOUBLE_PTR: return TRACEY_TYPE_DOUBLE;
        default: return TRACEY_TYPE_INT;
    }
}

static bool tracey_types_assignable(enum tracey_type_kind dst, enum tracey_type_kind src)
{
    if (dst == src)
        return true;
    /* Allow integer-to-integer assignment (with possible narrowing/widening). */
    if (tracey_is_integer(dst) && tracey_is_integer(src))
        return true;
    return false;
}

/*
 * tracey_semantic_expr_struct_id - resolve the struct id of an expression.
 *
 * Returns the index of the struct definition selected by the expression
 * when it has struct value type, or -1 otherwise. Identifiers resolve
 * through the current scope; member expressions resolve through their base.
 * The function reports no diagnostics; callers decide how to report a
 * missing struct.
 */
static int tracey_semantic_expr_struct_id(struct tracey_semantic_analyzer *sa,
                                          const struct tracey_expr *e)
{
    if (e == NULL)
        return -1;

    if (e->kind == TRACEY_EXPR_IDENT)
    {
        struct tracey_semantic_symbol sym;

        if (tracey_semantic_lookup(sa, e->as.ident_expr.name, &sym) &&
            sym.type == TRACEY_TYPE_STRUCT)
            return sym.struct_id;
        return -1;
    }

    if (e->kind == TRACEY_EXPR_MEMBER)
    {
        int base_sid = tracey_semantic_expr_struct_id(sa, e->as.member_expr.base);
        const struct tracey_struct_def *def;
        size_t i;

        if (base_sid < 0 || sa->program == NULL)
            return -1;
        def = &sa->program->structs[base_sid];
        for (i = 0; i < def->member_count; i++)
        {
            if (def->members[i].name != NULL &&
                strcmp(def->members[i].name, e->as.member_expr.field) == 0)
                return def->members[i].struct_id;
        }
    }
    return -1;
}

/*
 * tracey_semantic_member - look up a member in the struct selected by base.
 *
 * base_sid must be a valid struct id. Returns a borrowed pointer to the
 * member, or NULL when the struct has no such member.
 */
static const struct tracey_struct_member *
tracey_semantic_member(struct tracey_semantic_analyzer *sa, int base_sid, const char *field)
{
    const struct tracey_struct_def *def;
    size_t i;

    if (sa->program == NULL || base_sid < 0 || (size_t)base_sid >= sa->program->struct_count)
        return NULL;
    def = &sa->program->structs[base_sid];
    for (i = 0; i < def->member_count; i++)
    {
        if (def->members[i].name != NULL && strcmp(def->members[i].name, field) == 0)
            return &def->members[i];
    }
    return NULL;
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
        case TRACEY_EXPR_CHAR:
            return TRACEY_TYPE_CHAR;
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
        case TRACEY_EXPR_MEMBER:
        {
            struct tracey_expr *base = e->as.member_expr.base;
            int base_sid;
            const struct tracey_struct_member *m;

            (void)tracey_semantic_check_expr(sa, base, fn);
            base_sid = tracey_semantic_expr_struct_id(sa, base);
            if (base_sid < 0)
            {
                tracey_semantic_error(sa, "Member access requires a struct value", e->loc);
                return TRACEY_TYPE_INT;
            }
            m = tracey_semantic_member(sa, base_sid, e->as.member_expr.field);
            if (m == NULL)
            {
                char buf[256];
                const char *tag = (sa->program != NULL)
                                      ? sa->program->structs[base_sid].name : "";

                snprintf(buf, sizeof(buf), "Struct '%s' has no member named '%s'",
                         tag ? tag : "",
                         e->as.member_expr.field ? e->as.member_expr.field : "");
                tracey_semantic_error(sa, buf, e->loc);
                return TRACEY_TYPE_INT;
            }
            return m->type;
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
                if (tracey_is_numeric(t))
                    return t;
                tracey_semantic_error(sa, "Unary '-' requires numeric", e->loc);
                return TRACEY_TYPE_INT;
            }
            if (e->as.unary_expr.op == TRACEY_UNARY_LOG_NOT)
            {
                if (t != TRACEY_TYPE_BOOL)
                    tracey_semantic_error(sa, "'!' requires bool", e->loc);
                return TRACEY_TYPE_BOOL;
            }
            if (e->as.unary_expr.op == TRACEY_UNARY_PRE_INC ||
                e->as.unary_expr.op == TRACEY_UNARY_PRE_DEC ||
                e->as.unary_expr.op == TRACEY_UNARY_POST_INC ||
                e->as.unary_expr.op == TRACEY_UNARY_POST_DEC)
            {
                if (!tracey_is_integer(t))
                    tracey_semantic_error(sa, "Increment/decrement requires integer", e->loc);
                /* operand must be assignable */
                if (!(e->as.unary_expr.operand->kind == TRACEY_EXPR_IDENT ||
                      e->as.unary_expr.operand->kind == TRACEY_EXPR_INDEX ||
                      e->as.unary_expr.operand->kind == TRACEY_EXPR_DEREF))
                    tracey_semantic_error(sa, "Increment/decrement requires assignable lvalue", e->loc);
                return t;
            }
            if (e->as.unary_expr.op == TRACEY_UNARY_BIT_NOT)
            {
                if (tracey_is_integer(t))
                    return t;
                tracey_semantic_error(sa, "Unary '~' requires integer", e->loc);
                return TRACEY_TYPE_INT;
            }
            return TRACEY_TYPE_INT;
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
                if (lhs && lhs->kind == TRACEY_EXPR_MEMBER)
                {
                    enum tracey_type_kind lhs_t = tracey_semantic_check_expr(sa, lhs, fn);
                    enum tracey_type_kind rt = tracey_semantic_check_expr(sa, e->as.binary_expr.right, fn);
                    if (!tracey_types_assignable(lhs_t, rt))
                        tracey_semantic_error(sa, "Type mismatch in assignment", e->loc);
                    return lhs_t;
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
                    case TRACEY_BINARY_ASSIGN:
                    case TRACEY_BINARY_ADD_ASSIGN:
                    case TRACEY_BINARY_SUB_ASSIGN:
                    case TRACEY_BINARY_MUL_ASSIGN:
                    case TRACEY_BINARY_DIV_ASSIGN:
                    case TRACEY_BINARY_MOD_ASSIGN:
                    case TRACEY_BINARY_SHL_ASSIGN:
                    case TRACEY_BINARY_SHR_ASSIGN:
                    case TRACEY_BINARY_AND_ASSIGN:
                    case TRACEY_BINARY_OR_ASSIGN:
                    case TRACEY_BINARY_XOR_ASSIGN:
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
                                enum tracey_type_kind rt2 = tracey_semantic_check_expr(sa, e->as.binary_expr.right, fn);
                                if (!tracey_types_assignable(sym.type, rt2))
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
                            else if (bt == TRACEY_TYPE_LONG_PTR) elem = TRACEY_TYPE_LONG;
                            else if (bt == TRACEY_TYPE_UNSIGNED_LONG_PTR) elem = TRACEY_TYPE_UNSIGNED_LONG;
                            else if (bt == TRACEY_TYPE_LONG_LONG_PTR) elem = TRACEY_TYPE_LONG_LONG;
                            else if (bt == TRACEY_TYPE_UNSIGNED_LONG_LONG_PTR) elem = TRACEY_TYPE_UNSIGNED_LONG_LONG;
                            else if (bt == TRACEY_TYPE_SHORT_PTR) elem = TRACEY_TYPE_SHORT;
                            else if (bt == TRACEY_TYPE_UNSIGNED_SHORT_PTR) elem = TRACEY_TYPE_UNSIGNED_SHORT;
                            else if (bt == TRACEY_TYPE_SIZE_T_PTR) elem = TRACEY_TYPE_SIZE_T;
                            else if (bt != TRACEY_TYPE_INT_PTR)
                                tracey_semantic_error(sa, "Subscript requires array or pointer", lhs->loc);
                            {
                                enum tracey_type_kind rt2 = tracey_semantic_check_expr(sa, e->as.binary_expr.right, fn);
                                if (!tracey_types_assignable(elem, rt2))
                                    tracey_semantic_error(sa, "Type mismatch in assignment", e->loc);
                            }
                            return elem;
                        }
                        if (lhs && lhs->kind == TRACEY_EXPR_DEREF)
                        {
                            enum tracey_type_kind pt = tracey_semantic_check_expr(sa, lhs->as.deref_expr.operand, fn);
                            enum tracey_type_kind elem = tracey_pointer_element_type(pt);
                            if (!tracey_is_pointer(pt))
                                tracey_semantic_error(sa, "Cannot dereference non-pointer", lhs->loc);
                            {
                                enum tracey_type_kind rt2 = tracey_semantic_check_expr(sa, e->as.binary_expr.right, fn);
                                if (!tracey_types_assignable(elem, rt2))
                                    tracey_semantic_error(sa, "Type mismatch in assignment", e->loc);
                            }
                            return elem;
                        }
                        if (lhs && lhs->kind == TRACEY_EXPR_MEMBER)
                        {
                            tracey_semantic_error(sa,
                                "Compound assignment to a struct member is not supported yet",
                                e->loc);
                            (void)tracey_semantic_check_expr(sa, lhs, fn);
                            (void)tracey_semantic_check_expr(sa, e->as.binary_expr.right, fn);
                            return TRACEY_TYPE_INT;
                        }
                        tracey_semantic_error(sa, "Invalid assignment target", e->loc);
                        (void)tracey_semantic_check_expr(sa, e->as.binary_expr.right, fn);
                        return TRACEY_TYPE_INT;
                    }
                    case TRACEY_BINARY_ADD:
                    case TRACEY_BINARY_SUB:
                    case TRACEY_BINARY_MUL:
                    case TRACEY_BINARY_DIV:
                        if (lt == TRACEY_TYPE_DOUBLE || rt == TRACEY_TYPE_DOUBLE)
                        {
                            if (tracey_is_numeric(lt) && tracey_is_numeric(rt))
                                return TRACEY_TYPE_DOUBLE;
                            tracey_semantic_error(sa, "Arithmetic requires numeric operands", e->loc);
                            return TRACEY_TYPE_DOUBLE;
                        }
                        if (lt == TRACEY_TYPE_FLOAT || rt == TRACEY_TYPE_FLOAT)
                        {
                            if (tracey_is_numeric(lt) && tracey_is_numeric(rt))
                                return TRACEY_TYPE_FLOAT;
                            tracey_semantic_error(sa, "Arithmetic requires numeric operands", e->loc);
                            return TRACEY_TYPE_FLOAT;
                        }
                        if (tracey_is_integer(lt) && tracey_is_integer(rt))
                        {
                            /* Promote to the wider integer type. */
                            if (lt == TRACEY_TYPE_LONG_LONG || rt == TRACEY_TYPE_LONG_LONG)
                                return TRACEY_TYPE_LONG_LONG;
                            if (lt == TRACEY_TYPE_LONG || rt == TRACEY_TYPE_LONG)
                                return TRACEY_TYPE_LONG;
                            if (lt == TRACEY_TYPE_SIZE_T || rt == TRACEY_TYPE_SIZE_T)
                                return TRACEY_TYPE_SIZE_T;
                            return TRACEY_TYPE_INT;
                        }
                        tracey_semantic_error(sa, "Arithmetic requires numeric operands", e->loc);
                        return TRACEY_TYPE_INT;
                    case TRACEY_BINARY_MOD:
                        if (tracey_is_integer(lt) && tracey_is_integer(rt))
                            return TRACEY_TYPE_INT;
                        tracey_semantic_error(sa, "Modulo requires integer operands", e->loc);
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
                    case TRACEY_BINARY_BIT_AND:
                    case TRACEY_BINARY_BIT_OR:
                    case TRACEY_BINARY_BIT_XOR:
                        if (tracey_is_integer(lt) && tracey_is_integer(rt))
                        {
                            if (lt == TRACEY_TYPE_LONG_LONG || rt == TRACEY_TYPE_LONG_LONG)
                                return TRACEY_TYPE_LONG_LONG;
                            if (lt == TRACEY_TYPE_LONG || rt == TRACEY_TYPE_LONG)
                                return TRACEY_TYPE_LONG;
                            return TRACEY_TYPE_INT;
                        }
                        tracey_semantic_error(sa, "Bitwise requires integer operands", e->loc);
                        return TRACEY_TYPE_INT;
                    case TRACEY_BINARY_SHIFT_LEFT:
                    case TRACEY_BINARY_SHIFT_RIGHT:
                        if (tracey_is_integer(lt) && tracey_is_integer(rt))
                            return lt;
                        tracey_semantic_error(sa, "Shift requires integer operands", e->loc);
                        return TRACEY_TYPE_INT;
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
            if (name && strcmp(name, "puts") == 0)
            {
                if (e->as.call_expr.arg_count != 1)
                    tracey_semantic_error(sa, "Wrong number of arguments to 'puts'", e->loc);
                if (e->as.call_expr.arg_count >= 1)
                {
                    enum tracey_type_kind t = tracey_semantic_check_expr(sa, e->as.call_expr.args[0], fn);
                    if (t != TRACEY_TYPE_CHAR_PTR)
                        tracey_semantic_error(sa, "puts requires char*", e->as.call_expr.args[0]->loc);
                }
                return TRACEY_TYPE_INT;
            }
            if (name && strcmp(name, "putchar") == 0)
            {
                if (e->as.call_expr.arg_count != 1)
                    tracey_semantic_error(sa, "Wrong number of arguments to 'putchar'", e->loc);
                if (e->as.call_expr.arg_count >= 1)
                    tracey_semantic_check_expr(sa, e->as.call_expr.args[0], fn);
                return TRACEY_TYPE_INT;
            }
            if (name && strcmp(name, "getchar") == 0)
            {
                if (e->as.call_expr.arg_count != 0)
                    tracey_semantic_error(sa, "Wrong number of arguments to 'getchar'", e->loc);
                return TRACEY_TYPE_INT;
            }
            if (name && strcmp(name, "fget") == 0)
            {
                if (e->as.call_expr.arg_count != 1)
                    tracey_semantic_error(sa, "Wrong number of arguments to 'fget'", e->loc);
                if (e->as.call_expr.arg_count >= 1)
                {
                    enum tracey_type_kind t = tracey_semantic_check_expr(sa, e->as.call_expr.args[0], fn);
                    if (t != TRACEY_TYPE_CHAR_PTR)
                        tracey_semantic_error(sa, "fget requires char* buffer", e->as.call_expr.args[0]->loc);
                }
                return TRACEY_TYPE_CHAR_PTR;
            }
            if (name && strcmp(name, "fgets") == 0)
            {
                if (e->as.call_expr.arg_count != 2)
                    tracey_semantic_error(sa, "Wrong number of arguments to 'fgets'", e->loc);
                if (e->as.call_expr.arg_count >= 1)
                {
                    enum tracey_type_kind t = tracey_semantic_check_expr(sa, e->as.call_expr.args[0], fn);
                    if (t != TRACEY_TYPE_CHAR_PTR)
                        tracey_semantic_error(sa, "fgets requires char* buffer", e->as.call_expr.args[0]->loc);
                }
                if (e->as.call_expr.arg_count >= 2)
                {
                    enum tracey_type_kind t = tracey_semantic_check_expr(sa, e->as.call_expr.args[1], fn);
                    if (t != TRACEY_TYPE_INT)
                        tracey_semantic_error(sa, "fgets size must be int", e->as.call_expr.args[1]->loc);
                }
                return TRACEY_TYPE_CHAR_PTR;
            }
            if (name && (strcmp(name, "scanf") == 0 || strcmp(name, "scanfs") == 0))
            {
                if (e->as.call_expr.arg_count < 2)
                    tracey_semantic_error(sa, "Wrong number of arguments", e->loc);
                if (e->as.call_expr.arg_count >= 1)
                {
                    enum tracey_type_kind t = tracey_semantic_check_expr(sa, e->as.call_expr.args[0], fn);
                    if (t != TRACEY_TYPE_CHAR_PTR)
                        tracey_semantic_error(sa, "Format must be char*", e->as.call_expr.args[0]->loc);
                }
                for (size_t ai = 1; ai < e->as.call_expr.arg_count; ai++)
                    tracey_semantic_check_expr(sa, e->as.call_expr.args[ai], fn);
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
        case TRACEY_EXPR_TERNARY:
        {
            enum tracey_type_kind ct = tracey_semantic_check_expr(sa, e->as.ternary_expr.condition, fn);
            if (ct != TRACEY_TYPE_BOOL)
                tracey_semantic_error(sa, "Ternary condition must be bool", e->loc);
            enum tracey_type_kind tt = tracey_semantic_check_expr(sa, e->as.ternary_expr.then_branch, fn);
            enum tracey_type_kind et = tracey_semantic_check_expr(sa, e->as.ternary_expr.else_branch, fn);
            if (tt != et)
                tracey_semantic_error(sa, "Ternary branches must have same type", e->loc);
            return tt;
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
            if (vt == TRACEY_TYPE_STRUCT && s->as.var_decl_stmt.array_size > 0)
            {
                tracey_semantic_error(sa, "Arrays of structs are not supported yet", s->loc);
                return;
            }
            if (vt == TRACEY_TYPE_STRUCT && s->as.var_decl_stmt.init != NULL)
            {
                tracey_semantic_error(sa, "Struct initialization is not supported yet", s->loc);
                return;
            }
            if (!tracey_semantic_declare(sa, s->as.var_decl_stmt.name, sym, s->loc))
                return;
            if (s->as.var_decl_stmt.init)
            {
                enum tracey_type_kind t = tracey_semantic_check_expr(sa, s->as.var_decl_stmt.init, fn);
                if (!tracey_types_assignable(vt, t))
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
                        if (new_items == NULL) return;
                        sa->goto_labels = new_items;
                        sa->goto_label_cap = new_cap;
                    }
                    sa->goto_labels[sa->goto_label_count] = strdup(s->as.goto_stmt.label);
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
                        if (new_items == NULL) { tracey_semantic_check_stmt(sa, s->as.label_stmt.stmt, fn, expected_return); return; }
                        sa->declared_labels = new_items;
                        sa->declared_label_cap = new_cap;
                    }
                    sa->declared_labels[sa->declared_label_count] = strdup(s->as.label_stmt.label);
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
        sym.struct_id = -1;
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

bool tracey_semantic_analyze(struct tracey_semantic_analyzer *sa, struct tracey_program *program)
{
    size_t i;
    if (sa == NULL || program == NULL)
        return false;
    tracey_semantic_free(sa);
    tracey_semantic_init(sa);
    sa->program = program;
    tracey_semantic_push_scope(sa);

    for (i = 0; i < program->function_count; i++)
    {
        struct tracey_function_decl *fn = program->functions[i];
        struct tracey_semantic_symbol sym;
        sym.type = fn->return_type;
        sym.is_function = true;
        sym.arity = (int)fn->param_count;
        sym.struct_id = -1;
        tracey_semantic_declare(sa, fn->name, sym, fn->loc);
    }
    for (i = 0; i < program->function_count; i++)
        tracey_semantic_check_function(sa, program->functions[i]);

    tracey_semantic_pop_scope(sa);
    return !sa->had_error;
}
