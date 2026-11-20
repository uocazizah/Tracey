#include "semantic_internal.h"

#include "tracey/semantic.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

char *tracey_strdup_local(const char *s)
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


void tracey_semantic_scope_free(struct tracey_semantic_scope *scope)
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

void tracey_semantic_string_array_free(char **items, size_t count)
{
    size_t i;
    if (items == NULL)
        return;
    for (i = 0; i < count; i++)
        free(items[i]);
    free(items);
}


void tracey_semantic_error(struct tracey_semantic_analyzer *sa, const char *msg,
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

void tracey_semantic_warning(struct tracey_semantic_analyzer *sa, const char *msg,
                                    struct tracey_source_loc loc)
{
    struct tracey_semantic_warning *new_warnings;
    char *copy;
    if (sa == NULL)
        return;
    sa->had_warning = true;
    if (sa->warning_count == sa->warning_cap)
    {
        size_t new_cap = sa->warning_cap ? sa->warning_cap * 2 : 8;
        new_warnings = (struct tracey_semantic_warning *)realloc(sa->warnings,
                                                                  new_cap * sizeof(*new_warnings));
        if (new_warnings == NULL)
            return;
        sa->warnings = new_warnings;
        sa->warning_cap = new_cap;
    }
    copy = tracey_strdup_local(msg);
    if (copy == NULL)
        return;
    sa->warnings[sa->warning_count].message = copy;
    sa->warnings[sa->warning_count].line = loc.line;
    sa->warnings[sa->warning_count].column = loc.column;
    sa->warning_count++;
}

void tracey_semantic_push_scope(struct tracey_semantic_analyzer *sa)
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

void tracey_semantic_pop_scope(struct tracey_semantic_analyzer *sa)
{
    if (sa->scope_count == 0)
        return;
    sa->scope_count--;
    tracey_semantic_scope_free(&sa->scopes[sa->scope_count]);
}

bool tracey_semantic_declare(struct tracey_semantic_analyzer *sa, const char *name,
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

bool tracey_semantic_lookup(const struct tracey_semantic_analyzer *sa, const char *name,
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

enum tracey_type_kind tracey_semantic_check_expr(struct tracey_semantic_analyzer *sa,
                                                        struct tracey_expr *e,
                                                        const struct tracey_function_decl *fn);

void tracey_semantic_check_stmt(struct tracey_semantic_analyzer *sa, struct tracey_stmt *s,
                                       const struct tracey_function_decl *fn,
                                       enum tracey_type_kind expected_return);

bool tracey_is_numeric(enum tracey_type_kind t)
{
    return t == TRACEY_TYPE_INT || t == TRACEY_TYPE_UNSIGNED_INT ||
           t == TRACEY_TYPE_BOOL || t == TRACEY_TYPE_CHAR ||
           t == TRACEY_TYPE_SHORT || t == TRACEY_TYPE_UNSIGNED_SHORT ||
           t == TRACEY_TYPE_LONG || t == TRACEY_TYPE_UNSIGNED_LONG ||
           t == TRACEY_TYPE_LONG_LONG || t == TRACEY_TYPE_UNSIGNED_LONG_LONG ||
           t == TRACEY_TYPE_SIZE_T || t == TRACEY_TYPE_FLOAT || t == TRACEY_TYPE_DOUBLE;
}

bool tracey_is_integer(enum tracey_type_kind t)
{
    return t == TRACEY_TYPE_INT || t == TRACEY_TYPE_UNSIGNED_INT ||
           t == TRACEY_TYPE_CHAR || t == TRACEY_TYPE_SHORT ||
           t == TRACEY_TYPE_UNSIGNED_SHORT || t == TRACEY_TYPE_LONG ||
           t == TRACEY_TYPE_UNSIGNED_LONG || t == TRACEY_TYPE_LONG_LONG ||
           t == TRACEY_TYPE_UNSIGNED_LONG_LONG || t == TRACEY_TYPE_SIZE_T;
}

bool tracey_is_pointer(enum tracey_type_kind t)
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

enum tracey_type_kind tracey_pointer_element_type(enum tracey_type_kind t)
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

bool tracey_types_assignable(enum tracey_type_kind dst, enum tracey_type_kind src)
{
    if (dst == src)
        return true;
    /* Allow integer-to-integer assignment (with possible narrowing/widening). */
    if (tracey_is_integer(dst) && tracey_is_integer(src))
        return true;
    /* Function pointer types are assignable only to an identical signature. */
    if (dst == TRACEY_TYPE_FUNC_PTR && src == TRACEY_TYPE_FUNC_PTR)
        return true;
    return false;
}

bool tracey_semantic_is_narrowing_conversion(enum tracey_type_kind dst, enum tracey_type_kind src)
{
    /*
     * A narrowing conversion is an assignment from a floating-point value
     * or a wide integer value into an int destination. The check is
     * deliberately conservative: only int destinations are considered
     * because the task scope is float->int and long->int conversions.
     */
    if (dst != TRACEY_TYPE_INT)
        return false;
    if (src == TRACEY_TYPE_FLOAT || src == TRACEY_TYPE_DOUBLE)
        return true;
    if (src == TRACEY_TYPE_LONG || src == TRACEY_TYPE_UNSIGNED_LONG ||
        src == TRACEY_TYPE_LONG_LONG || src == TRACEY_TYPE_UNSIGNED_LONG_LONG)
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
int tracey_semantic_expr_struct_id(struct tracey_semantic_analyzer *sa,
                                          const struct tracey_expr *e)
{
    if (e == NULL)
        return -1;

    if (e->kind == TRACEY_EXPR_IDENT)
    {
        struct tracey_semantic_symbol sym;

        if (tracey_semantic_lookup(sa, e->as.ident_expr.name, &sym) &&
            (sym.type == TRACEY_TYPE_STRUCT || sym.type == TRACEY_TYPE_STRUCT_PTR))
            return sym.struct_id;
        return -1;
    }

    if (e->kind == TRACEY_EXPR_INDEX)
    {
        int base_sid = tracey_semantic_expr_struct_id(sa, e->as.index_expr.base);

        if (base_sid < 0)
        {
            /* The base may be a struct array variable. */
            if (e->as.index_expr.base->kind == TRACEY_EXPR_IDENT)
            {
                struct tracey_semantic_symbol sym;

                if (tracey_semantic_lookup(sa, e->as.index_expr.base->as.ident_expr.name, &sym) &&
                    sym.type == TRACEY_TYPE_STRUCT)
                    return sym.struct_id;
            }
        }
        return base_sid;
    }

    if (e->kind == TRACEY_EXPR_DEREF)
    {
        if (e->as.deref_expr.operand != NULL)
        {
            /* If the operand is a struct pointer variable, return its
               pointee struct id. */
            if (e->as.deref_expr.operand->kind == TRACEY_EXPR_IDENT)
            {
                struct tracey_semantic_symbol sym;
                if (tracey_semantic_lookup(sa, e->as.deref_expr.operand->as.ident_expr.name, &sym) &&
                    (sym.type == TRACEY_TYPE_STRUCT_PTR || sym.type == TRACEY_TYPE_STRUCT))
                    return sym.struct_id;
            }
            return tracey_semantic_expr_struct_id(sa, e->as.deref_expr.operand);
        }
        return -1;
    }

    if (e->kind == TRACEY_EXPR_ADDR)
    {
        if (e->as.addr_expr.operand != NULL)
            return tracey_semantic_expr_struct_id(sa, e->as.addr_expr.operand);
        return -1;
    }

    if (e->kind == TRACEY_EXPR_ADDR)
    {
        if (e->as.addr_expr.operand != NULL)
            return tracey_semantic_expr_struct_id(sa, e->as.addr_expr.operand);
        return -1;
    }

    if (e->kind == TRACEY_EXPR_CALL)
    {
        const char *cname = e->as.call_expr.callee ? e->as.call_expr.callee : "";
        size_t fi;

        if (sa->program != NULL)
        {
            for (fi = 0; fi < sa->program->function_count; fi++)
            {
                if (sa->program->functions[fi] != NULL &&
                    sa->program->functions[fi]->name != NULL &&
                    strcmp(sa->program->functions[fi]->name, cname) == 0 &&
                    sa->program->functions[fi]->return_type == TRACEY_TYPE_STRUCT)
                    return sa->program->functions[fi]->return_struct_id;
            }
        }
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
const struct tracey_struct_member *
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

