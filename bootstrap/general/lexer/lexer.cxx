#include <tracey/lexer.hxx>

#include <tracey/config.hxx>

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
    return at_end();
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

char lexer::advance() noexcept
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

bool lexer::match(char expected) noexcept
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

    /* Capture the token's start position *before* consuming the first
     * character: `advance()` mutates line_/column_, so we snapshot them
     * here and thread them through to make_token(). This keeps line/column
     * tracking O(1) per token instead of rescanning the source. */
    const std::size_t start_pos = position_;
    const std::size_t start_line = line_;
    const std::size_t start_column = column_;

    if (at_end()) {
        return make_token(token_type::eof, start_pos, 0, start_line, start_column);
    }

    const char c = advance();

    switch (c) {
        case '(': return make_token(token_type::lparen, start_pos, 1, start_line, start_column);
        case ')': return make_token(token_type::rparen, start_pos, 1, start_line, start_column);
        case '{': return make_token(token_type::lbrace, start_pos, 1, start_line, start_column);
        case '}': return make_token(token_type::rbrace, start_pos, 1, start_line, start_column);
        case ';': return make_token(token_type::semicolon, start_pos, 1, start_line, start_column);

        default:
            if (is_digit(c)) {
                return scan_number(start_pos, start_line, start_column);
            }
            if (is_ident_start(c)) {
                return scan_identifier(start_pos, start_line, start_column);
            }
            TRACEY_TRACE("[lexer] unexpected character '{}' at {}:{}\n",
                         c, start_line, start_column);
            return make_error_token(start_pos, start_line, start_column);
    }
}

token lexer::scan_identifier(std::size_t start_pos, std::size_t start_line,
                             std::size_t start_column)
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

    return make_token(type, start_pos, length, start_line, start_column);
}

token lexer::scan_number(std::size_t start_pos, std::size_t start_line,
                         std::size_t start_column)
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
    return make_token(token_type::integer, start_pos, length, start_line, start_column);
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
                        std::size_t length, std::size_t line,
                        std::size_t column) const noexcept
{
    token t;
    t.type = type;
    t.lexeme = content_.substr(start_pos, length);
    /* Line/column were snapshotted by the caller before the token was
     * consumed — no rescan needed. */
    t.line = line;
    t.column = column;

    return t;
}

token lexer::make_error_token(std::size_t start_pos, std::size_t line,
                              std::size_t column) const noexcept
{
    return make_token(token_type::error_, start_pos, 0, line, column);
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
