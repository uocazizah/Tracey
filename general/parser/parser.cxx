#include <tracey/parser.hxx>

#include <tracey/lexer.hxx>

#include <memory>
#include <string>
#include <utility>

namespace tracey {

parser::parser(lexer& lx) : lx_(lx) {
    advance();
}

void parser::advance() {
    if (has_current_) {
        current_ = lx_.next();
    } else {
        current_ = lx_.next();
        has_current_ = true;
    }
}

const token& parser::peek() const { return current_; }

bool parser::check(token_type t) const {
    return has_current_ && current_.type == t;
}

bool parser::match(token_type t) {
    if (check(t)) { advance(); return true; }
    return false;
}

bool parser::expect(token_type t, const char* msg) {
    if (check(t)) { advance(); return true; }
    error(msg, current_);
    return false;
}

void parser::error(const std::string& msg, const token& t) {
    parse_error e;
    e.message = msg;
    e.line = t.line;
    e.column = t.column;
    errors_.push_back(e);
    had_error_ = true;
}

void parser::synchronize() {
    while (has_current_ && current_.type != token_type::eof) {
        if (current_.type == token_type::semicolon) { advance(); return; }
        if (current_.type == token_type::rbrace) return;
        if (current_.type == token_type::keyword_if ||
            current_.type == token_type::keyword_while ||
            current_.type == token_type::keyword_return ||
            current_.type == token_type::keyword_int ||
            current_.type == token_type::keyword_bool ||
            current_.type == token_type::keyword_void ||
            current_.type == token_type::keyword_char ||
            current_.type == token_type::keyword_float ||
            current_.type == token_type::keyword_double) return;
        advance();
    }
}

bool parser::parse(program& out) {
    while (has_current_ && current_.type != token_type::eof) {
        if (current_.type == token_type::error_) {
            error("unexpected lexer error", current_);
            advance();
            continue;
        }
        if (!parse_function(out)) {
            synchronize();
            if (check(token_type::semicolon)) advance();
            if (check(token_type::rbrace)) advance();
        }
    }
    return !had_error_;
}

type_kind parser::parse_type() {
    if (check(token_type::keyword_int)) {
        advance();
        if (check(token_type::star)) { advance(); return type_kind::int_ptr; }
        return type_kind::int_;
    }
    if (check(token_type::keyword_float)) {
        advance();
        if (check(token_type::star)) { advance(); return type_kind::float_ptr; }
        return type_kind::float_;
    }
    if (check(token_type::keyword_double)) {
        advance();
        if (check(token_type::star)) { advance(); return type_kind::double_ptr; }
        return type_kind::double_;
    }
    if (check(token_type::keyword_bool)) { advance(); return type_kind::bool_; }
    if (check(token_type::keyword_void)) { advance(); return type_kind::void_; }
    if (check(token_type::keyword_char)) {
        advance();
        if (check(token_type::star)) { advance(); return type_kind::char_ptr; }
        return type_kind::char_;
    }
    error("expected type", current_);
    return type_kind::int_;
}

bool parser::parse_function(program& p) {
    source_loc loc{current_.line, current_.column};
    type_kind rt = parse_type();
    if (had_error_ && !check(token_type::identifier)) { /* continue */ }
    if (!check(token_type::identifier)) {
        error("expected function name", current_);
        return false;
    }
    std::string name(current_.lexeme);
    advance();
    if (!expect(token_type::lparen, "expected '('")) return false;
    std::vector<std::pair<type_kind,std::string>> params;
    bool variadic = false;
    if (!check(token_type::rparen)) {
        for (;;) {
            if (check(token_type::dot)) {
                // variadic: ...
                match(token_type::dot);
                match(token_type::dot);
                match(token_type::dot);
                variadic = true;
                break;
            }
            type_kind pt = parse_type();
            /** `void` as the sole parameter means "no parameters" (C-style). */
            if (pt == type_kind::void_ && !check(token_type::identifier)) {
                if (!params.empty() || variadic) {
                    error("void parameter must be the only parameter", current_);
                }
                break;
            }
            if (!check(token_type::identifier)) { error("expected parameter name", current_); break; }
            std::string pname(current_.lexeme);
            advance();
            params.emplace_back(pt, pname);
            if (!match(token_type::comma)) break;
        }
    }
    if (!expect(token_type::rparen, "expected ')'")) return false;
    std::unique_ptr<stmt> body;
    if (check(token_type::semicolon)) {
        advance();
        body = nullptr;
    } else {
        body = parse_block();
        if (!body) return false;
    }
    auto fn = std::make_unique<function_decl>();
    fn->return_type = rt;
    fn->name = name;
    fn->params = std::move(params);
    fn->variadic = variadic;
    fn->body = std::move(body);
    fn->loc = loc;
    p.functions.push_back(std::move(fn));
    return true;
}

std::unique_ptr<stmt> parser::parse_block() {
    source_loc loc{current_.line, current_.column};
    if (!expect(token_type::lbrace, "expected '{'")) return nullptr;
    auto b = std::make_unique<block_stmt>();
    b->loc = loc;
    while (has_current_ && current_.type != token_type::rbrace && current_.type != token_type::eof) {
        auto s = parse_statement();
        if (s) b->statements.push_back(std::move(s));
        else { synchronize(); if (check(token_type::semicolon)) advance(); }
    }
    expect(token_type::rbrace, "expected '}'");
    return b;
}

std::unique_ptr<stmt> parser::parse_statement() {
    if (check(token_type::lbrace)) return parse_block();
    if (check(token_type::keyword_if)) return parse_if();
    if (check(token_type::keyword_while)) return parse_while();
    if (check(token_type::keyword_return)) return parse_return();
    if (check(token_type::keyword_int) || check(token_type::keyword_bool) || check(token_type::keyword_void) ||
        check(token_type::keyword_char) || check(token_type::keyword_float) || check(token_type::keyword_double)) {
        type_kind t = parse_type();
        if (!check(token_type::identifier)) { error("expected variable name", current_); synchronize(); return nullptr; }
        token name_tok = current_;
        std::string name(current_.lexeme);
        advance();
        auto vd = std::make_unique<var_decl_stmt>();
        vd->type = t;
        vd->name = name;
        vd->loc = {name_tok.line, name_tok.column};
        if (check(token_type::lbracket)) {
            advance();
            std::size_t sz = 0;
            if (check(token_type::integer)) { sz = std::stoull(std::string(current_.lexeme)); advance(); }
            vd->array_size = sz;
            expect(token_type::rbracket, "expected ']'");
        }
        if (match(token_type::assign)) {
            auto e = parse_expression();
            if (!e) return nullptr;
            vd->init = std::move(e);
        }
        if (!expect(token_type::semicolon, "expected ';'")) return nullptr;
        return vd;
    }
    if (check(token_type::semicolon)) { advance(); return std::make_unique<block_stmt>(); }
    auto e = parse_expression();
    if (!e) return nullptr;
    if (!expect(token_type::semicolon, "expected ';'")) return nullptr;
    auto es = std::make_unique<expr_stmt>(std::move(e));
    return es;
}

std::unique_ptr<stmt> parser::parse_if() {
    source_loc loc{current_.line, current_.column};
    advance(); // if
    if (!expect(token_type::lparen, "expected '('")) return nullptr;
    auto cond = parse_expression();
    if (!cond) return nullptr;
    if (!expect(token_type::rparen, "expected ')'")) return nullptr;
    auto thenb = parse_statement();
    if (!thenb) return nullptr;
    std::unique_ptr<stmt> elseb;
    if (match(token_type::keyword_else)) {
        elseb = parse_statement();
        if (!elseb) return nullptr;
    }
    auto is = std::make_unique<if_stmt>();
    is->condition = std::move(cond);
    is->then_branch = std::move(thenb);
    is->else_branch = std::move(elseb);
    is->loc = loc;
    return is;
}

std::unique_ptr<stmt> parser::parse_while() {
    source_loc loc{current_.line, current_.column};
    advance(); // while
    if (!expect(token_type::lparen, "expected '('")) return nullptr;
    auto cond = parse_expression();
    if (!cond) return nullptr;
    if (!expect(token_type::rparen, "expected ')'")) return nullptr;
    auto body = parse_statement();
    if (!body) return nullptr;
    auto ws = std::make_unique<while_stmt>();
    ws->condition = std::move(cond);
    ws->body = std::move(body);
    ws->loc = loc;
    return ws;
}

std::unique_ptr<stmt> parser::parse_return() {
    source_loc loc{current_.line, current_.column};
    advance(); // return
    std::unique_ptr<expr> val;
    if (!check(token_type::semicolon)) {
        val = parse_expression();
        if (!val) return nullptr;
    }
    if (!expect(token_type::semicolon, "expected ';'")) return nullptr;
    auto rs = std::make_unique<return_stmt>();
    rs->value = std::move(val);
    rs->loc = loc;
    return rs;
}

std::unique_ptr<expr> parser::parse_expression() { return parse_assignment(); }

std::unique_ptr<expr> parser::parse_assignment() {
    auto left = parse_logical_or();
    if (!left) return nullptr;
    if (check(token_type::assign)) {
        if (dynamic_cast<ident_expr*>(left.get()) ||
            dynamic_cast<index_expr*>(left.get()) ||
            dynamic_cast<deref_expr*>(left.get())) {
            advance();
            auto rhs = parse_assignment();
            if (!rhs) return nullptr;
            source_loc loc = left->loc;
            return std::make_unique<binary_expr>(binary_op::assign,
                std::move(left), std::move(rhs), loc);
        }
    }
    return left;
}

std::unique_ptr<expr> parser::parse_logical_or() {
    auto left = parse_logical_and();
    if (!left) return nullptr;
    while (check(token_type::pipe_pipe)) {
        advance();
        auto right = parse_logical_and();
        if (!right) return nullptr;
        source_loc loc = left->loc;
        left = std::make_unique<binary_expr>(binary_op::log_or, std::move(left), std::move(right), loc);
    }
    return left;
}

std::unique_ptr<expr> parser::parse_logical_and() {
    auto left = parse_equality();
    if (!left) return nullptr;
    while (check(token_type::amp_amp)) {
        advance();
        auto right = parse_equality();
        if (!right) return nullptr;
        source_loc loc = left->loc;
        left = std::make_unique<binary_expr>(binary_op::log_and, std::move(left), std::move(right), loc);
    }
    return left;
}

std::unique_ptr<expr> parser::parse_equality() {
    auto left = parse_relational();
    if (!left) return nullptr;
    while (check(token_type::equal_equal) || check(token_type::bang_equal)) {
        binary_op op = check(token_type::equal_equal) ? binary_op::eq : binary_op::ne;
        advance();
        auto right = parse_relational();
        if (!right) return nullptr;
        source_loc loc = left->loc;
        left = std::make_unique<binary_expr>(op, std::move(left), std::move(right), loc);
    }
    return left;
}

std::unique_ptr<expr> parser::parse_relational() {
    auto left = parse_additive();
    if (!left) return nullptr;
    while (check(token_type::less) || check(token_type::greater) ||
           check(token_type::less_equal) || check(token_type::greater_equal)) {
        binary_op op;
        if (check(token_type::less)) op = binary_op::lt;
        else if (check(token_type::greater)) op = binary_op::gt;
        else if (check(token_type::less_equal)) op = binary_op::le;
        else op = binary_op::ge;
        advance();
        auto right = parse_additive();
        if (!right) return nullptr;
        source_loc loc = left->loc;
        left = std::make_unique<binary_expr>(op, std::move(left), std::move(right), loc);
    }
    return left;
}

std::unique_ptr<expr> parser::parse_additive() {
    auto left = parse_multiplicative();
    if (!left) return nullptr;
    while (check(token_type::plus) || check(token_type::minus)) {
        binary_op op = check(token_type::plus) ? binary_op::add : binary_op::sub;
        advance();
        auto right = parse_multiplicative();
        if (!right) return nullptr;
        source_loc loc = left->loc;
        left = std::make_unique<binary_expr>(op, std::move(left), std::move(right), loc);
    }
    return left;
}

std::unique_ptr<expr> parser::parse_multiplicative() {
    auto left = parse_unary();
    if (!left) return nullptr;
    while (check(token_type::star) || check(token_type::slash) || check(token_type::percent)) {
        binary_op op;
        if (check(token_type::star)) op = binary_op::mul;
        else if (check(token_type::slash)) op = binary_op::div;
        else op = binary_op::mod;
        advance();
        auto right = parse_unary();
        if (!right) return nullptr;
        source_loc loc = left->loc;
        left = std::make_unique<binary_expr>(op, std::move(left), std::move(right), loc);
    }
    return left;
}

std::unique_ptr<expr> parser::parse_unary() {
    if (check(token_type::minus)) {
        advance();
        auto operand = parse_unary();
        if (!operand) return nullptr;
        return std::make_unique<unary_expr>(unary_op::neg, std::move(operand));
    }
    if (check(token_type::bang)) {
        advance();
        auto operand = parse_unary();
        if (!operand) return nullptr;
        return std::make_unique<unary_expr>(unary_op::log_not, std::move(operand));
    }
    if (check(token_type::ampersand)) {
        advance();
        auto operand = parse_unary();
        if (!operand) return nullptr;
        return std::make_unique<addr_expr>(std::move(operand));
    }
    if (check(token_type::star)) {
        advance();
        auto operand = parse_unary();
        if (!operand) return nullptr;
        return std::make_unique<deref_expr>(std::move(operand));
    }
    auto primary = parse_primary();
    while (has_current_ && check(token_type::lbracket)) {
        advance();
        auto idx = parse_expression();
        if (!idx) return nullptr;
        if (!expect(token_type::rbracket, "expected ']'")) return nullptr;
        primary = std::make_unique<index_expr>(std::move(primary), std::move(idx));
    }
    return primary;
}

std::unique_ptr<expr> parser::parse_primary() {
    if (check(token_type::integer)) {
        token t = current_;
        advance();
        long long v = 0;
        for (char c : t.lexeme) v = v*10 + (c-'0');
        return std::make_unique<int_expr>(v, source_loc{t.line,t.column});
    }
    if (check(token_type::floating)) {
        token t = current_;
        advance();
        std::string lex(t.lexeme);
        bool is_float = false;
        if (!lex.empty() && (lex.back() == 'f' || lex.back() == 'F')) {
            is_float = true;
            lex.pop_back();
        }
        double v = std::stod(lex);
        return std::make_unique<real_expr>(v, is_float, source_loc{t.line,t.column});
    }
    if (check(token_type::string)) {
        token t = current_;
        advance();
        std::string s(t.lexeme);
        if (s.size() >= 2 && s.front() == '"' && s.back() == '"') s = s.substr(1, s.size()-2);
        // decode escapes
        std::string out;
        for (size_t i = 0; i < s.size();) {
            if (s[i] == '\\' && i + 1 < s.size()) {
                char c = s[i+1];
                if (c == 'n') { out.push_back('\n'); i += 2; continue; }
                if (c == 't') { out.push_back('\t'); i += 2; continue; }
                if (c == 'r') { out.push_back('\r'); i += 2; continue; }
                if (c == '0') { out.push_back('\0'); i += 2; continue; }
                if (c == '\\') { out.push_back('\\'); i += 2; continue; }
                if (c == '"') { out.push_back('"'); i += 2; continue; }
                out.push_back(s[i]); i++;
            } else {
                out.push_back(s[i]); i++;
            }
        }
        return std::make_unique<string_expr>(out, source_loc{t.line,t.column});
    }
    if (check(token_type::keyword_true)) { advance(); return std::make_unique<bool_expr>(true); }
    if (check(token_type::keyword_false)) { advance(); return std::make_unique<bool_expr>(false); }
    if (check(token_type::identifier)) {
        std::string name(current_.lexeme);
        source_loc loc{current_.line,current_.column};
        advance();
        if (check(token_type::lparen)) {
            return parse_call(name, token{token_type::identifier, token_error::none, {}, loc.line, loc.column});
        }
        return std::make_unique<ident_expr>(name, loc);
    }
    if (check(token_type::lparen)) {
        advance();
        auto e = parse_expression();
        if (!e) return nullptr;
        if (!expect(token_type::rparen, "expected ')'")) return nullptr;
        return e;
    }
    error("expected expression", current_);
    return nullptr;
}

std::unique_ptr<expr> parser::parse_call(const std::string& name, const token&) {
    source_loc loc{};
    if (!expect(token_type::lparen, "expected '('")) return nullptr;
    std::vector<std::unique_ptr<expr>> args;
    if (!check(token_type::rparen)) {
        for (;;) {
            auto e = parse_expression();
            if (!e) return nullptr;
            args.push_back(std::move(e));
            if (!match(token_type::comma)) break;
        }
    }
    if (!expect(token_type::rparen, "expected ')'")) return nullptr;
    return std::make_unique<call_expr>(name, std::move(args), loc);
}

} // namespace tracey
