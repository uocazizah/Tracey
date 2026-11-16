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
    TRACEY_TYPE_UNSIGNED_INT,
    TRACEY_TYPE_BOOL,
    TRACEY_TYPE_CHAR,
    TRACEY_TYPE_SHORT,
    TRACEY_TYPE_UNSIGNED_SHORT,
    TRACEY_TYPE_LONG,
    TRACEY_TYPE_UNSIGNED_LONG,
    TRACEY_TYPE_LONG_LONG,
    TRACEY_TYPE_UNSIGNED_LONG_LONG,
    TRACEY_TYPE_SIZE_T,
    TRACEY_TYPE_CHAR_PTR,
    TRACEY_TYPE_INT_PTR,
    TRACEY_TYPE_SHORT_PTR,
    TRACEY_TYPE_UNSIGNED_SHORT_PTR,
    TRACEY_TYPE_LONG_PTR,
    TRACEY_TYPE_UNSIGNED_LONG_PTR,
    TRACEY_TYPE_LONG_LONG_PTR,
    TRACEY_TYPE_UNSIGNED_LONG_LONG_PTR,
    TRACEY_TYPE_SIZE_T_PTR,
    TRACEY_TYPE_FLOAT,
    TRACEY_TYPE_DOUBLE,
    TRACEY_TYPE_FLOAT_PTR,
    TRACEY_TYPE_DOUBLE_PTR,
    /*
     * Aggregate value type. This kind carries no layout by itself: the
     * struct_id recorded alongside the type selects the matching entry in
     * tracey_program::structs. A struct type is only meaningful while the
     * program that declares it is alive.
     */
    TRACEY_TYPE_STRUCT,
    TRACEY_TYPE_STRUCT_PTR,
    /*
     * Function pointer type. The pointee signature is stored separately in a
     * tracey_func_sig because it cannot be encoded in the flat enum.
     */
    TRACEY_TYPE_FUNC_PTR,
};

enum tracey_unary_op
{
    TRACEY_UNARY_NEG = 0,
    TRACEY_UNARY_LOG_NOT,
    TRACEY_UNARY_BIT_NOT,
    TRACEY_UNARY_PRE_INC,
    TRACEY_UNARY_PRE_DEC,
    TRACEY_UNARY_POST_INC,
    TRACEY_UNARY_POST_DEC,
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
    TRACEY_BINARY_BIT_AND,
    TRACEY_BINARY_BIT_OR,
    TRACEY_BINARY_BIT_XOR,
    TRACEY_BINARY_SHIFT_LEFT,
    TRACEY_BINARY_SHIFT_RIGHT,
    TRACEY_BINARY_ASSIGN,
    TRACEY_BINARY_ADD_ASSIGN,
    TRACEY_BINARY_SUB_ASSIGN,
    TRACEY_BINARY_MUL_ASSIGN,
    TRACEY_BINARY_DIV_ASSIGN,
    TRACEY_BINARY_MOD_ASSIGN,
    TRACEY_BINARY_SHL_ASSIGN,
    TRACEY_BINARY_SHR_ASSIGN,
    TRACEY_BINARY_AND_ASSIGN,
    TRACEY_BINARY_OR_ASSIGN,
    TRACEY_BINARY_XOR_ASSIGN,
};

enum tracey_expr_kind
{
    TRACEY_EXPR_INT = 0,
    TRACEY_EXPR_BOOL,
    TRACEY_EXPR_REAL,
    TRACEY_EXPR_STRING,
    TRACEY_EXPR_CHAR,
    TRACEY_EXPR_IDENT,
    TRACEY_EXPR_UNARY,
    TRACEY_EXPR_BINARY,
    TRACEY_EXPR_INDEX,
    TRACEY_EXPR_ADDR,
    TRACEY_EXPR_DEREF,
    TRACEY_EXPR_CALL,
    TRACEY_EXPR_TERNARY,
    TRACEY_EXPR_MEMBER,
    TRACEY_EXPR_INIT_LIST,
    TRACEY_EXPR_SIZEOF,
};

enum tracey_stmt_kind
{
    TRACEY_STMT_BLOCK = 0,
    TRACEY_STMT_VAR_DECL,
    TRACEY_STMT_EXPR,
    TRACEY_STMT_IF,
    TRACEY_STMT_WHILE,
    TRACEY_STMT_SWITCH,
    TRACEY_STMT_DO_WHILE,
    TRACEY_STMT_FOR,
    TRACEY_STMT_BREAK,
    TRACEY_STMT_CONTINUE,
    TRACEY_STMT_GOTO,
    TRACEY_STMT_LABEL,
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

struct tracey_char_expr
{
    int value;
};

struct tracey_ternary_expr
{
    struct tracey_expr *condition;
    struct tracey_expr *then_branch;
    struct tracey_expr *else_branch;
};

/*
 * Member access (base.field).
 *
 * base is the aggregate expression whose member is selected. field is the
 * member name exactly as written; the parser does not resolve it. The
 * semantic analyzer and the code generator each look the name up in the
 * struct definition selected by the base expression's struct id.
 */
struct tracey_member_expr
{
    struct tracey_expr *base;
    char *field;
};

struct tracey_init_list_expr
{
    struct tracey_expr **items;
    size_t count;
    size_t cap;
};

/*
 * sizeof expression.
 *
 * When is_type is true, the size refers to the type described by type,
 * struct_id, and fn_sig. Otherwise it refers to the type of operand, which
 * is owned by this node.
 */
struct tracey_sizeof_expr
{
    bool is_type;
    enum tracey_type_kind type;
    int struct_id;
    struct tracey_func_sig *fn_sig;
    struct tracey_expr *operand;
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
        struct tracey_char_expr char_expr;
        struct tracey_ident_expr ident_expr;
        struct tracey_unary_expr unary_expr;
        struct tracey_binary_expr binary_expr;
        struct tracey_index_expr index_expr;
        struct tracey_addr_expr addr_expr;
        struct tracey_deref_expr deref_expr;
        struct tracey_call_expr call_expr;
        struct tracey_ternary_expr ternary_expr;
        struct tracey_member_expr member_expr;
        struct tracey_init_list_expr init_list_expr;
        struct tracey_sizeof_expr sizeof_expr;
    } as;
};

struct tracey_block_stmt
{
    struct tracey_stmt **statements;
    size_t count;
};

struct tracey_func_sig
{
    enum tracey_type_kind return_type;
    enum tracey_type_kind *param_types;
    size_t param_count;
    size_t param_cap;
};

struct tracey_var_decl_stmt
{
    enum tracey_type_kind type;
    /*
     * Selects the struct definition when type is TRACEY_TYPE_STRUCT;
     * otherwise it is -1. The id indexes tracey_program::structs.
     */
    int struct_id;
    /*
     * Function pointer signature when type is TRACEY_TYPE_FUNC_PTR;
     * otherwise NULL. Owned by this declaration.
     */
    struct tracey_func_sig *fn_sig;
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

struct tracey_switch_case
{
    long value;
    bool has_value; /* false for default */
    struct tracey_stmt *body;
};

struct tracey_switch_stmt
{
    struct tracey_expr *condition;
    struct tracey_switch_case *cases;
    size_t case_count;
    size_t case_cap;
    struct tracey_stmt *default_body;
};

struct tracey_do_while_stmt
{
    struct tracey_stmt *body;
    struct tracey_expr *condition;
};

struct tracey_for_stmt
{
    struct tracey_stmt *init;     /* NULL when no initializer */
    struct tracey_expr *condition; /* NULL when no condition */
    struct tracey_expr *update;    /* NULL when no update */
    struct tracey_stmt *body;
};

struct tracey_goto_stmt
{
    char *label;
};

struct tracey_label_stmt
{
    char *label;
    struct tracey_stmt *stmt;
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
        struct tracey_switch_stmt switch_stmt;
        struct tracey_do_while_stmt do_while_stmt;
        struct tracey_for_stmt for_stmt;
        struct tracey_goto_stmt goto_stmt;
        struct tracey_label_stmt label_stmt;
        struct tracey_return_stmt return_stmt;
    } as;
};

struct tracey_param
{
    enum tracey_type_kind type;
    /*
     * Index of the struct definition when type is TRACEY_TYPE_STRUCT or
     * TRACEY_TYPE_STRUCT_PTR; -1 otherwise.
     */
    int struct_id;
    /*
     * Function pointer signature when type is TRACEY_TYPE_FUNC_PTR;
     * otherwise NULL. Owned by this parameter.
     */
    struct tracey_func_sig *fn_sig;
    char *name;
};

struct tracey_function_decl
{
    enum tracey_type_kind return_type;
    int return_struct_id;
    /*
     * Function pointer signature when return_type is TRACEY_TYPE_FUNC_PTR;
     * otherwise NULL. Owned by this declaration.
     */
    struct tracey_func_sig *return_fn_sig;
    char *name;
    struct tracey_param *params;
    size_t param_count;
    bool variadic;
    struct tracey_stmt *body; /* NULL for declarations without body */
    struct tracey_source_loc loc;
};

/*
 * Struct member layout.
 *
 * Only scalar members are accepted in the current stage: every member
 * occupies eight bytes so the code generator can address it with a single
 * 64-bit load or store. There is no packing or alignment analysis yet, so
 * struct_id is always -1 here and offset is simply eight times the member
 * index.
 */
struct tracey_struct_member
{
    enum tracey_type_kind type;
    int struct_id;
    char *name;
    size_t offset;
};

/*
 * Struct definition.
 *
 * name is the tag used after the struct keyword. members is a growable
 * array owned by the definition. size is the total storage in bytes; it is
 * recomputed as members are appended. Definitions are owned by the
 * tracey_program that declares them and are released by tracey_program_free.
 */
struct tracey_struct_def
{
    char *name;
    struct tracey_struct_member *members;
    size_t member_count;
    size_t member_cap;
    size_t size;
    size_t alignment;
};

struct tracey_program
{
    struct tracey_function_decl **functions;
    size_t function_count;
    struct tracey_struct_def *structs;
    size_t struct_count;
    size_t struct_cap;
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
void tracey_func_sig_free(struct tracey_func_sig *sig);

/*
 * tracey_program_add_struct - declare a struct tag and return its id.
 *
 * The tag name is copied. Declaring the same tag twice creates a second,
 * independent definition; the parser resolves tags by first match. Returns
 * the new definition's index, or -1 on allocation failure.
 */
int tracey_program_add_struct(struct tracey_program *program, const char *name);

/*
 * tracey_program_find_struct - look up a struct tag by name.
 *
 * Returns a borrowed pointer to the definition, or NULL when the tag is
 * unknown. The pointer is invalidated by any later add_struct call that
 * causes the definitions array to grow.
 */
struct tracey_struct_def *tracey_program_find_struct(const struct tracey_program *program,
                                                     const char *name);

/*
 * tracey_program_find_struct_id - resolve a struct tag to its index.
 *
 * Returns the definition index, or -1 when the tag is unknown.
 */
int tracey_program_find_struct_id(const struct tracey_program *program, const char *name);

/*
 * tracey_struct_add_member - append a member to a struct definition.
 *
 * The member name is copied. offset is set to the current size of the
 * definition and the definition's size grows by size, rounded up to a
 * multiple of eight so that later members stay 64-bit aligned. size must
 * be greater than zero. Returns false on allocation failure, when size is
 * zero, or when a member with the same name already exists.
 */
bool tracey_struct_add_member(struct tracey_struct_def *def, const char *name,
                              enum tracey_type_kind type, int struct_id,
                              size_t size, size_t alignment);

#ifdef __cplusplus
}
#endif

#endif /* TRACEY_AST_H */
