#include "tracey/ast.h"

#include <stdlib.h>
#include <string.h>

struct tracey_expr *tracey_expr_create(enum tracey_expr_kind kind, struct tracey_source_loc loc)
{
    struct tracey_expr *expr = (struct tracey_expr *)calloc(1, sizeof(*expr));
    if (expr == NULL)
        return NULL;
    expr->kind = kind;
    expr->loc = loc;
    return expr;
}

struct tracey_stmt *tracey_stmt_create(enum tracey_stmt_kind kind, struct tracey_source_loc loc)
{
    struct tracey_stmt *stmt = (struct tracey_stmt *)calloc(1, sizeof(*stmt));
    if (stmt == NULL)
        return NULL;
    stmt->kind = kind;
    stmt->loc = loc;
    return stmt;
}

struct tracey_function_decl *tracey_function_decl_create(void)
{
    return (struct tracey_function_decl *)calloc(1, sizeof(struct tracey_function_decl));
}

void tracey_program_init(struct tracey_program *program)
{
    if (program == NULL)
        return;
    program->functions = NULL;
    program->function_count = 0;
    program->structs = NULL;
    program->struct_count = 0;
    program->struct_cap = 0;
}

int tracey_program_add_struct(struct tracey_program *program, const char *name)
{
    struct tracey_struct_def *def;

    if (program == NULL || name == NULL)
        return -1;
    if (program->struct_count == program->struct_cap)
    {
        size_t cap = program->struct_cap ? program->struct_cap * 2 : 8;
        struct tracey_struct_def *p =
            (struct tracey_struct_def *)realloc(program->structs, cap * sizeof(*p));
        if (p == NULL)
            return -1;
        memset(p + program->struct_cap, 0, (cap - program->struct_cap) * sizeof(*p));
        program->structs = p;
        program->struct_cap = cap;
    }
    def = &program->structs[program->struct_count];
    def->name = strdup(name);
    if (def->name == NULL)
        return -1;
    def->members = NULL;
    def->member_count = 0;
    def->member_cap = 0;
    def->size = 0;
    return (int)program->struct_count++;
}

struct tracey_struct_def *tracey_program_find_struct(const struct tracey_program *program,
                                                     const char *name)
{
    size_t i;

    if (program == NULL || name == NULL)
        return NULL;
    for (i = 0; i < program->struct_count; i++)
    {
        if (program->structs[i].name != NULL && strcmp(program->structs[i].name, name) == 0)
            return (struct tracey_struct_def *)&program->structs[i];
    }
    return NULL;
}

int tracey_program_find_struct_id(const struct tracey_program *program, const char *name)
{
    size_t i;

    if (program == NULL || name == NULL)
        return -1;
    for (i = 0; i < program->struct_count; i++)
    {
        if (program->structs[i].name != NULL && strcmp(program->structs[i].name, name) == 0)
            return (int)i;
    }
    return -1;
}

bool tracey_struct_add_member(struct tracey_struct_def *def, const char *name,
                              enum tracey_type_kind type, int struct_id, size_t size)
{
    size_t i;
    size_t padded;

    if (def == NULL || name == NULL || size == 0)
        return false;
    for (i = 0; i < def->member_count; i++)
    {
        if (def->members[i].name != NULL && strcmp(def->members[i].name, name) == 0)
            return false;
    }
    if (def->member_count == def->member_cap)
    {
        size_t cap = def->member_cap ? def->member_cap * 2 : 8;
        struct tracey_struct_member *p =
            (struct tracey_struct_member *)realloc(def->members, cap * sizeof(*p));
        if (p == NULL)
            return false;
        def->members = p;
        def->member_cap = cap;
    }
    def->members[def->member_count].type = type;
    def->members[def->member_count].struct_id = struct_id;
    def->members[def->member_count].name = strdup(name);
    if (def->members[def->member_count].name == NULL)
        return false;
    def->members[def->member_count].offset = def->size;
    def->member_count++;
    padded = size;
    if (padded % 8 != 0)
        padded += 8 - (padded % 8);
    def->size += padded;
    return true;
}

void tracey_expr_destroy(struct tracey_expr *expr)
{
    size_t i;

    if (expr == NULL)
        return;

    switch (expr->kind)
    {
        case TRACEY_EXPR_INT:
        case TRACEY_EXPR_BOOL:
        case TRACEY_EXPR_REAL:
            break;
        case TRACEY_EXPR_STRING:
            free(expr->as.string_expr.value);
            break;
        case TRACEY_EXPR_CHAR:
            break;
        case TRACEY_EXPR_IDENT:
            free(expr->as.ident_expr.name);
            break;
        case TRACEY_EXPR_UNARY:
            tracey_expr_destroy(expr->as.unary_expr.operand);
            break;
        case TRACEY_EXPR_BINARY:
            tracey_expr_destroy(expr->as.binary_expr.left);
            tracey_expr_destroy(expr->as.binary_expr.right);
            break;
        case TRACEY_EXPR_INDEX:
            tracey_expr_destroy(expr->as.index_expr.base);
            tracey_expr_destroy(expr->as.index_expr.index);
            break;
        case TRACEY_EXPR_ADDR:
            tracey_expr_destroy(expr->as.addr_expr.operand);
            break;
        case TRACEY_EXPR_DEREF:
            tracey_expr_destroy(expr->as.deref_expr.operand);
            break;
        case TRACEY_EXPR_CALL:
            for (i = 0; i < expr->as.call_expr.arg_count; i++)
                tracey_expr_destroy(expr->as.call_expr.args[i]);
            free(expr->as.call_expr.args);
            free(expr->as.call_expr.callee);
            break;
        case TRACEY_EXPR_TERNARY:
            tracey_expr_destroy(expr->as.ternary_expr.condition);
            tracey_expr_destroy(expr->as.ternary_expr.then_branch);
            tracey_expr_destroy(expr->as.ternary_expr.else_branch);
            break;
        case TRACEY_EXPR_MEMBER:
            tracey_expr_destroy(expr->as.member_expr.base);
            free(expr->as.member_expr.field);
            break;
        case TRACEY_EXPR_INIT_LIST:
        {
            size_t i;
            for (i = 0; i < expr->as.init_list_expr.count; i++)
                tracey_expr_destroy(expr->as.init_list_expr.items[i]);
            free(expr->as.init_list_expr.items);
            break;
        }
        default:
            break;
    }

    free(expr);
}

void tracey_stmt_destroy(struct tracey_stmt *stmt)
{
    size_t i;

    if (stmt == NULL)
        return;

    switch (stmt->kind)
    {
        case TRACEY_STMT_BLOCK:
            for (i = 0; i < stmt->as.block_stmt.count; i++)
                tracey_stmt_destroy(stmt->as.block_stmt.statements[i]);
            free(stmt->as.block_stmt.statements);
            break;
        case TRACEY_STMT_VAR_DECL:
            free(stmt->as.var_decl_stmt.name);
            tracey_expr_destroy(stmt->as.var_decl_stmt.init);
            tracey_func_sig_free(stmt->as.var_decl_stmt.fn_sig);
            break;
        case TRACEY_STMT_EXPR:
            tracey_expr_destroy(stmt->as.expr_stmt.expression);
            break;
        case TRACEY_STMT_IF:
            tracey_expr_destroy(stmt->as.if_stmt.condition);
            tracey_stmt_destroy(stmt->as.if_stmt.then_branch);
            tracey_stmt_destroy(stmt->as.if_stmt.else_branch);
            break;
        case TRACEY_STMT_WHILE:
            tracey_expr_destroy(stmt->as.while_stmt.condition);
            tracey_stmt_destroy(stmt->as.while_stmt.body);
            break;
        case TRACEY_STMT_DO_WHILE:
            tracey_stmt_destroy(stmt->as.do_while_stmt.body);
            tracey_expr_destroy(stmt->as.do_while_stmt.condition);
            break;
        case TRACEY_STMT_SWITCH:
        {
            size_t ci;
            tracey_expr_destroy(stmt->as.switch_stmt.condition);
            for (ci = 0; ci < stmt->as.switch_stmt.case_count; ci++)
                tracey_stmt_destroy(stmt->as.switch_stmt.cases[ci].body);
            free(stmt->as.switch_stmt.cases);
            tracey_stmt_destroy(stmt->as.switch_stmt.default_body);
            break;
        }
        case TRACEY_STMT_FOR:
            tracey_stmt_destroy(stmt->as.for_stmt.init);
            tracey_expr_destroy(stmt->as.for_stmt.condition);
            tracey_expr_destroy(stmt->as.for_stmt.update);
            tracey_stmt_destroy(stmt->as.for_stmt.body);
            break;
        case TRACEY_STMT_BREAK:
        case TRACEY_STMT_CONTINUE:
            break;
        case TRACEY_STMT_GOTO:
            free(stmt->as.goto_stmt.label);
            break;
        case TRACEY_STMT_LABEL:
            free(stmt->as.label_stmt.label);
            tracey_stmt_destroy(stmt->as.label_stmt.stmt);
            break;
        case TRACEY_STMT_RETURN:
            tracey_expr_destroy(stmt->as.return_stmt.value);
            break;
        default:
            break;
    }

    free(stmt);
}

void tracey_function_decl_destroy(struct tracey_function_decl *fn)
{
    size_t i;

    if (fn == NULL)
        return;

    free(fn->name);
    for (i = 0; i < fn->param_count; i++)
    {
        free(fn->params[i].name);
        tracey_func_sig_free(fn->params[i].fn_sig);
    }
    free(fn->params);
    tracey_stmt_destroy(fn->body);
    free(fn);
}

void tracey_func_sig_free(struct tracey_func_sig *sig)
{
    if (sig == NULL)
        return;
    free(sig->param_types);
    free(sig);
}

void tracey_program_free(struct tracey_program *program)
{
    size_t i;

    if (program == NULL)
        return;

    for (i = 0; i < program->function_count; i++)
        tracey_function_decl_destroy(program->functions[i]);
    free(program->functions);
    program->functions = NULL;
    program->function_count = 0;

    for (i = 0; i < program->struct_count; i++)
    {
        size_t m;

        free(program->structs[i].name);
        for (m = 0; m < program->structs[i].member_count; m++)
            free(program->structs[i].members[m].name);
        free(program->structs[i].members);
    }
    free(program->structs);
    program->structs = NULL;
    program->struct_count = 0;
    program->struct_cap = 0;
}
