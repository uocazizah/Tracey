#ifndef TRACEY_SEMANTIC_HXX
#define TRACEY_SEMANTIC_HXX

#include <tracey/ast.hxx>

#include <string>
#include <vector>

namespace tracey {

struct semantic_error {
    std::string message;
    std::size_t line{1};
    std::size_t column{1};
};

class semantic_analyzer {
public:
    bool analyze(program& p);

    [[nodiscard]] bool had_error() const noexcept { return had_error_; }
    [[nodiscard]] const std::vector<semantic_error>& errors() const noexcept { return errors_; }

private:
    struct symbol { type_kind type; bool is_function; int arity; };

    bool had_error_{false};
    std::vector<semantic_error> errors_;

    // scopes: vector of maps
    std::vector<std::vector<std::pair<std::string, symbol>>> scopes_;

    void error(const std::string& msg, const source_loc& loc);
    void push_scope();
    void pop_scope();
    bool declare(const std::string& name, symbol sym, const source_loc& loc);
    bool lookup(const std::string& name, symbol& out) const;

    type_kind check_expr(expr* e, const function_decl* fn);
    void check_stmt(stmt* s, const function_decl* fn, type_kind expected_return);
    void check_function(function_decl* f);
};

} // namespace tracey

#endif
