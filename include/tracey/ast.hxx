#ifndef TRACEY_AST_HXX
#define TRACEY_AST_HXX

#include <cstddef>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace tracey
{

enum class type_kind
{
    void_,
    int_,
    bool_,
    char_,
    char_ptr,
    int_ptr,
    float_,
    double_,
    float_ptr,
    double_ptr
};

struct source_loc
{
    std::size_t line{1};
    std::size_t column{1};
};

struct expr
{
    virtual ~expr() = default;
    source_loc loc{};
};
struct stmt
{
    virtual ~stmt() = default;
    source_loc loc{};
};

struct function_decl
{
    type_kind return_type{type_kind::void_};
    std::string name;
    std::vector<std::pair<type_kind, std::string>> params;
    bool variadic{false};
    std::unique_ptr<stmt> body;
    source_loc loc{};
};

struct program
{
    std::vector<std::unique_ptr<function_decl>> functions;
};

struct int_expr : expr
{
    long long value{0};
    explicit int_expr(long long v, source_loc l = {}) : value(v)
    {
        loc = l;
    }
};
struct bool_expr : expr
{
    bool value{false};
    explicit bool_expr(bool v, source_loc l = {}) : value(v)
    {
        loc = l;
    }
};
struct real_expr : expr
{
    double value{0.0};
    bool is_float{false};
    explicit real_expr(double v, bool f = false, source_loc l = {}) : value(v), is_float(f)
    {
        loc = l;
    }
};
struct string_expr : expr
{
    std::string value;
    explicit string_expr(std::string v, source_loc l = {}) : value(std::move(v))
    {
        loc = l;
    }
};
struct ident_expr : expr
{
    std::string name;
    explicit ident_expr(std::string n, source_loc l = {}) : name(std::move(n))
    {
        loc = l;
    }
};

enum class unary_op
{
    neg,
    log_not
};
struct unary_expr : expr
{
    unary_op op{unary_op::neg};
    std::unique_ptr<expr> operand;
    unary_expr(unary_op o, std::unique_ptr<expr> e, source_loc l = {})
        : op(o), operand(std::move(e))
    {
        loc = l;
    }
};

enum class binary_op
{
    add,
    sub,
    mul,
    div,
    mod,
    eq,
    ne,
    lt,
    gt,
    le,
    ge,
    log_and,
    log_or,
    assign
};
struct binary_expr : expr
{
    binary_op op{binary_op::add};
    std::unique_ptr<expr> left, right;
    binary_expr(binary_op o, std::unique_ptr<expr> l, std::unique_ptr<expr> r, source_loc loc_ = {})
        : op(o), left(std::move(l)), right(std::move(r))
    {
        loc = loc_;
    }
};

struct index_expr : expr
{
    std::unique_ptr<expr> base;
    std::unique_ptr<expr> index;
    index_expr(std::unique_ptr<expr> b, std::unique_ptr<expr> i, source_loc l = {})
        : base(std::move(b)), index(std::move(i))
    {
        loc = l;
    }
};
struct addr_expr : expr
{
    std::unique_ptr<expr> operand;
    explicit addr_expr(std::unique_ptr<expr> o, source_loc l = {}) : operand(std::move(o))
    {
        loc = l;
    }
};
struct deref_expr : expr
{
    std::unique_ptr<expr> operand;
    explicit deref_expr(std::unique_ptr<expr> o, source_loc l = {}) : operand(std::move(o))
    {
        loc = l;
    }
};

struct call_expr : expr
{
    std::string callee;
    std::vector<std::unique_ptr<expr>> args;
    call_expr(std::string c, std::vector<std::unique_ptr<expr>> a, source_loc l = {})
        : callee(std::move(c)), args(std::move(a))
    {
        loc = l;
    }
};

struct block_stmt : stmt
{
    std::vector<std::unique_ptr<stmt>> statements;
};
struct var_decl_stmt : stmt
{
    type_kind type{type_kind::int_};
    std::string name;
    std::size_t array_size{0};
    std::unique_ptr<expr> init;
};
struct expr_stmt : stmt
{
    std::unique_ptr<expr> expression;
    explicit expr_stmt(std::unique_ptr<expr> e) : expression(std::move(e))
    {
    }
};
struct if_stmt : stmt
{
    std::unique_ptr<expr> condition;
    std::unique_ptr<stmt> then_branch;
    std::unique_ptr<stmt> else_branch;
};
struct while_stmt : stmt
{
    std::unique_ptr<expr> condition;
    std::unique_ptr<stmt> body;
};
struct return_stmt : stmt
{
    std::unique_ptr<expr> value;
};

} // namespace tracey
#endif
