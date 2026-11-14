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
        free(fn->params[i].name);
    free(fn->params);
    tracey_stmt_destroy(fn->body);
    free(fn);
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
}
