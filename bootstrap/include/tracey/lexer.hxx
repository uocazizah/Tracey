#ifndef TRACEY_LEXER_HXX
#define TRACEY_LEXER_HXX

#include <cstddef>
#include <cstdio>
#include <optional>
#include <string_view>

namespace tracey {

/* Token types */
enum class token_type {
    eof = 0,
    identifier,
    integer,
    keyword_int,
    keyword_return,
    lparen,
    rparen,
    lbrace,
    rbrace,
    semicolon,
    error_,
};

/* Token structure.
 *
 * `lexeme` is a non-owning view into the source content that the producing
 * `lexer` was constructed from. It is only valid for as long as that content
 * stays alive (callers keep their `source` objects alive while lexing). */
struct token {
    token_type type{token_type::eof};
    std::string_view lexeme{};
    std::size_t line{1};
    std::size_t column{1};

    /* Print token debug info */
    void print_debug(std::FILE* out = nullptr) const;
};

/* Token type to string */
[[nodiscard]] std::string_view to_string(token_type type) noexcept;

/* Lexer: a movable, value-semantic cursor over a source string view. */
class lexer {
public:
    explicit lexer(std::string_view content) noexcept;

    /* Get the next token */
    [[nodiscard]] token next();

    /* Peek at the next token without consuming it */
    [[nodiscard]] token peek();

    /* Check if lexer is at end */
    [[nodiscard]] bool is_at_end() const noexcept;

    /* Get current line number */
    [[nodiscard]] std::size_t line() const noexcept;

    /* Get current column number */
    [[nodiscard]] std::size_t column() const noexcept;

    /* Print lexer debug info */
    void print_debug(std::FILE* out = nullptr) const;

private:
    /* Internal helpers */
    [[nodiscard]] bool at_end() const noexcept;
    [[nodiscard]] char current() const noexcept;
    [[nodiscard]] char current_next() const noexcept;
    char advance();
    bool match(char expected);
    void skip_whitespace() noexcept;

    [[nodiscard]] token scan_token();
    [[nodiscard]] token scan_identifier(std::size_t start_pos);
    [[nodiscard]] token scan_number(std::size_t start_pos);

    [[nodiscard]] token make_token(token_type type, std::size_t start_pos,
                                   std::size_t length) const noexcept;
    [[nodiscard]] token make_error_token(std::size_t start_pos) const noexcept;

    [[nodiscard]] static token_type check_keyword(std::string_view text) noexcept;

    std::string_view content_;
    std::size_t position_{0};
    std::size_t line_{1};
    std::size_t column_{1};
    std::optional<token> peeked_{};
};

} /* namespace tracey */

#endif /* TRACEY_LEXER_HXX */
