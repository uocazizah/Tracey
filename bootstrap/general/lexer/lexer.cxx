#include <tracey/lexer.hxx>

#include <array>

namespace {

/* Character classification without <cctype>: <cctype> has UB on negative
 * (non-EOF) char values, and these are branch-free and locale-independent. */
[[nodiscard]] constexpr bool is_ident_start(char c) noexcept
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
}

[[nodiscard]] constexpr bool is_ident_cont(char c) noexcept
{
    return is_ident_start(c) || (c >= '0' && c <= '9');
}

[[nodiscard]] constexpr bool is_digit(char c) noexcept
{
    return c >= '0' && c <= '9';
}

struct keyword_entry {
    std::string_view text;
    tracey::token_type type;
};

/* Keywords table */
constexpr std::array<keyword_entry, 2> keywords{{
    {"int", tracey::token_type::keyword_int},
    {"return", tracey::token_type::keyword_return},
}};

} /* anonymous namespace */

namespace tracey {

lexer::lexer(std::string_view content) noexcept
    : content_{content}
{
}

bool lexer::is_at_end() const noexcept
{
    return position_ >= content_.size();
}

std::size_t lexer::line() const noexcept
{
    return line_;
}

std::size_t lexer::column() const noexcept
{
    return column_;
}

token lexer::next()
{
    /* Return peeked token if available */
    if (peeked_) {
        token t = std::move(*peeked_);
        peeked_.reset();
        return t;
    }

    return scan_token();
}

token lexer::peek()
{
    if (!peeked_) {
        peeked_ = scan_token();
    }
    return *peeked_;
}

/* Internal helpers */

bool lexer::at_end() const noexcept
{
    return position_ >= content_.size();
}

char lexer::current() const noexcept
{
    if (at_end()) return '\0';
    return content_[position_];
}

char lexer::current_next() const noexcept
{
    if (position_ + 1 >= content_.size()) return '\0';
    return content_[position_ + 1];
}

char lexer::advance()
{
    if (at_end()) return '\0';

    const char c = content_[position_];
    position_++;

    if (c == '\n') {
        line_++;
        column_ = 1;
    } else {
        column_++;
    }

    return c;
}

bool lexer::match(char expected)
{
    if (at_end()) return false;
    if (content_[position_] != expected) return false;

    advance();
    return true;
}

void lexer::skip_whitespace() noexcept
{
    while (!at_end()) {
        const char c = current();
        if (c == ' ' || c == '\r' || c == '\t' || c == '\n') {
            advance();
        } else {
            break;
        }
    }
}

token lexer::scan_token()
{
    skip_whitespace();

    if (at_end()) {
        return make_token(token_type::eof, position_, 0);
    }

    const std::size_t start_pos = position_;
    const char c = advance();

    switch (c) {
        case '(': return make_token(token_type::lparen, start_pos, 1);
        case ')': return make_token(token_type::rparen, start_pos, 1);
        case '{': return make_token(token_type::lbrace, start_pos, 1);
        case '}': return make_token(token_type::rbrace, start_pos, 1);
        case ';': return make_token(token_type::semicolon, start_pos, 1);

        default:
            if (is_digit(c)) {
                return scan_number(start_pos);
            }
            if (is_ident_start(c)) {
                return scan_identifier(start_pos);
            }
            return make_error_token(start_pos);
    }
}

token lexer::scan_identifier(std::size_t start_pos)
{
    while (!at_end()) {
        const char c = current();
        if (is_ident_cont(c)) {
            advance();
        } else {
            break;
        }
    }

    const std::size_t length = position_ - start_pos;
    const token_type type = check_keyword(content_.substr(start_pos, length));

    return make_token(type, start_pos, length);
}

token lexer::scan_number(std::size_t start_pos)
{
    while (!at_end()) {
        const char c = current();
        if (is_digit(c)) {
            advance();
        } else {
            break;
        }
    }

    const std::size_t length = position_ - start_pos;
    return make_token(token_type::integer, start_pos, length);
}

token_type lexer::check_keyword(std::string_view text) noexcept
{
    for (const auto& kw : keywords) {
        if (kw.text == text) {
            return kw.type;
        }
    }
    return token_type::identifier;
}

token lexer::make_token(token_type type, std::size_t start_pos,
                        std::size_t length) const noexcept
{
    token t;
    t.type = type;
    t.lexeme = content_.substr(start_pos, length);

    /* Calculate line/column for the token start by rescanning from the
     * beginning (kept identical to the original C implementation). */
    std::size_t line = 1;
    std::size_t column = 1;

    for (std::size_t i = 0; i < start_pos && i < content_.size(); i++) {
        if (content_[i] == '\n') {
            line++;
            column = 1;
        } else {
            column++;
        }
    }

    t.line = line;
    t.column = column;

    return t;
}

token lexer::make_error_token(std::size_t start_pos) const noexcept
{
    return make_token(token_type::error_, start_pos, 0);
}

std::string_view to_string(token_type type) noexcept
{
#if defined(__clang__)
    #pragma clang diagnostic push
    #pragma clang diagnostic ignored "-Wcovered-switch-default"
#endif
    switch (type) {
        case token_type::eof:            return "EOF";
        case token_type::identifier:     return "IDENTIFIER";
        case token_type::integer:        return "INTEGER";
        case token_type::keyword_int:    return "KEYWORD_INT";
        case token_type::keyword_return: return "KEYWORD_RETURN";
        case token_type::lparen:         return "LPAREN";
        case token_type::rparen:         return "RPAREN";
        case token_type::lbrace:         return "LBRACE";
        case token_type::rbrace:         return "RBRACE";
        case token_type::semicolon:      return "SEMICOLON";
        case token_type::error_:         return "ERROR";
        default:                         return "UNKNOWN";
    }
#if defined(__clang__)
    #pragma clang diagnostic pop
#endif
}

} /* namespace tracey */
