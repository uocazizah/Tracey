#ifndef TRACEY_PARSER_HXX
#define TRACEY_PARSER_HXX

#include <tracey/ast.hxx>
#include <tracey/lexer.hxx>

#include <string>
#include <vector>

namespace tracey {

struct parse_error {
    std::string message;
    std::size_t line{1};
    std::size_t column{1};
};

class parser {
public:
    explicit parser(lexer& lx);

    /** Parse a full translation unit. Returns true on success. */
    bool parse(program& out);

    [[nodiscard]] bool had_error() const noexcept { return had_error_; }
    [[nodiscard]] const std::vector<parse_error>& errors() const noexcept { return errors_; }

private:
    lexer& lx_;
    bool had_error_{false};
    std::vector<parse_error> errors_;

    token current_{};
    bool has_current_{false};

    void advance();
    [[nodiscard]] const token& peek() const;
    [[nodiscard]] bool check(token_type t) const;
    bool match(token_type t);
    bool expect(token_type t, const char* msg);

    void error(const std::string& msg, const token& t);

    // grammar
    bool parse_function(program& p);
    type_kind parse_type();
    std::unique_ptr<stmt> parse_statement();
    std::unique_ptr<stmt> parse_block();
    std::unique_ptr<stmt> parse_if();
    std::unique_ptr<stmt> parse_while();
    std::unique_ptr<stmt> parse_return();
    std::unique_ptr<stmt> parse_var_decl(type_kind t, const token& first);
    std::unique_ptr<expr> parse_expression();
    std::unique_ptr<expr> parse_assignment();
    std::unique_ptr<expr> parse_logical_or();
    std::unique_ptr<expr> parse_logical_and();
    std::unique_ptr<expr> parse_equality();
    std::unique_ptr<expr> parse_relational();
    std::unique_ptr<expr> parse_additive();
    std::unique_ptr<expr> parse_multiplicative();
    std::unique_ptr<expr> parse_unary();
    std::unique_ptr<expr> parse_primary();
    std::unique_ptr<expr> parse_call(const std::string& name, const token& name_tok);

    void synchronize();
};

} // namespace tracey

#endif
