#ifndef TRACEY_AST_H
#define TRACEY_AST_H

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Source location attached to AST nodes for diagnostics. */
struct tracey_source_loc
{
    size_t line;
    size_t column;
};

/* Built-in and pointer type kinds. */
enum tracey_type_kind
{
    TRACEY_TYPE_VOID = 0,
    TRACEY_TYPE_INT,
    TRACEY_TYPE_BOOL,
    TRACEY_TYPE_CHAR,
    TRACEY_TYPE_CHAR_PTR,
    TRACEY_TYPE_INT_PTR,
    TRACEY_TYPE_FLOAT,
    TRACEY_TYPE_DOUBLE,
    TRACEY_TYPE_FLOAT_PTR,
    TRACEY_TYPE_DOUBLE_PTR,
};

enum tracey_unary_op
{
    TRACEY_UNARY_NEG = 0,
    TRACEY_UNARY_LOG_NOT,
};

enum tracey_binary_op
{
    TRACEY_BINARY_ADD = 0,
    TRACEY_BINARY_SUB,
    TRACEY_BINARY_MUL,
    TRACEY_BINARY_DIV,
    TRACEY_BINARY_MOD,
    TRACEY_BINARY_EQ,
    TRACEY_BINARY_NE,
    TRACEY_BINARY_LT,
    TRACEY_BINARY_GT,
    TRACEY_BINARY_LE,
    TRACEY_BINARY_GE,
    TRACEY_BINARY_LOG_AND,
    TRACEY_BINARY_LOG_OR,
    TRACEY_BINARY_ASSIGN,
};

enum tracey_expr_kind
{
    TRACEY_EXPR_INT = 0,
    TRACEY_EXPR_BOOL,
    TRACEY_EXPR_REAL,
    TRACEY_EXPR_STRING,
    TRACEY_EXPR_IDENT,
    TRACEY_EXPR_UNARY,
    TRACEY_EXPR_BINARY,
    TRACEY_EXPR_INDEX,
    TRACEY_EXPR_ADDR,
    TRACEY_EXPR_DEREF,
    TRACEY_EXPR_CALL,
};

enum tracey_stmt_kind
{
    TRACEY_STMT_BLOCK = 0,
    TRACEY_STMT_VAR_DECL,
    TRACEY_STMT_EXPR,
    TRACEY_STMT_IF,
    TRACEY_STMT_WHILE,
    TRACEY_STMT_RETURN,
};

struct tracey_expr;
struct tracey_stmt;

struct tracey_int_expr
{
    long long value;
};

struct tracey_bool_expr
{
    bool value;
};

struct tracey_real_expr
{
    double value;
    bool is_float;
};

struct tracey_string_expr
{
    char *value;
};

struct tracey_ident_expr
{
    char *name;
};

struct tracey_unary_expr
{
    enum tracey_unary_op op;
    struct tracey_expr *operand;
};

struct tracey_binary_expr
{
    enum tracey_binary_op op;
    struct tracey_expr *left;
    struct tracey_expr *right;
};

struct tracey_index_expr
{
    struct tracey_expr *base;
    struct tracey_expr *index;
};

struct tracey_addr_expr
{
    struct tracey_expr *operand;
};

struct tracey_deref_expr
{
    struct tracey_expr *operand;
};

struct tracey_call_expr
{
    char *callee;
    struct tracey_expr **args;
    size_t arg_count;
};

/*
 * Expression node.
 *
 * kind selects the active member of the union. The node owns any heap
 * memory referenced by its payload and must be released with
 * tracey_expr_destroy.
 */
struct tracey_expr
{
    enum tracey_expr_kind kind;
    struct tracey_source_loc loc;
    union
    {
        struct tracey_int_expr int_expr;
        struct tracey_bool_expr bool_expr;
        struct tracey_real_expr real_expr;
        struct tracey_string_expr string_expr;
        struct tracey_ident_expr ident_expr;
        struct tracey_unary_expr unary_expr;
        struct tracey_binary_expr binary_expr;
        struct tracey_index_expr index_expr;
        struct tracey_addr_expr addr_expr;
        struct tracey_deref_expr deref_expr;
        struct tracey_call_expr call_expr;
    } as;
};

struct tracey_block_stmt
{
    struct tracey_stmt **statements;
    size_t count;
};

struct tracey_var_decl_stmt
{
    enum tracey_type_kind type;
    char *name;
    size_t array_size;
    struct tracey_expr *init;
};

struct tracey_expr_stmt
{
    struct tracey_expr *expression;
};

struct tracey_if_stmt
{
    struct tracey_expr *condition;
    struct tracey_stmt *then_branch;
    struct tracey_stmt *else_branch;
};

struct tracey_while_stmt
{
    struct tracey_expr *condition;
    struct tracey_stmt *body;
};

struct tracey_return_stmt
{
    struct tracey_expr *value;
};

/*
 * Statement node.
 *
 * kind selects the active member of the union. The node owns its payload
 * and must be released with tracey_stmt_destroy.
 */
struct tracey_stmt
{
    enum tracey_stmt_kind kind;
    struct tracey_source_loc loc;
    union
    {
        struct tracey_block_stmt block_stmt;
        struct tracey_var_decl_stmt var_decl_stmt;
        struct tracey_expr_stmt expr_stmt;
        struct tracey_if_stmt if_stmt;
        struct tracey_while_stmt while_stmt;
        struct tracey_return_stmt return_stmt;
    } as;
};

struct tracey_param
{
    enum tracey_type_kind type;
    char *name;
};

struct tracey_function_decl
{
    enum tracey_type_kind return_type;
    char *name;
    struct tracey_param *params;
    size_t param_count;
    bool variadic;
    struct tracey_stmt *body; /* NULL for declarations without body */
    struct tracey_source_loc loc;
};

struct tracey_program
{
    struct tracey_function_decl **functions;
    size_t function_count;
};

/* Allocation and destruction helpers. */
struct tracey_expr *tracey_expr_create(enum tracey_expr_kind kind, struct tracey_source_loc loc);
struct tracey_stmt *tracey_stmt_create(enum tracey_stmt_kind kind, struct tracey_source_loc loc);
struct tracey_function_decl *tracey_function_decl_create(void);
void tracey_program_init(struct tracey_program *program);
void tracey_program_free(struct tracey_program *program);
void tracey_expr_destroy(struct tracey_expr *expr);
void tracey_stmt_destroy(struct tracey_stmt *stmt);
void tracey_function_decl_destroy(struct tracey_function_decl *fn);

#ifdef __cplusplus
}
#endif

#endif /* TRACEY_AST_H */
