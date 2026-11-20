#include "tracey/ast.h"

#include "ast_internal.h"

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
