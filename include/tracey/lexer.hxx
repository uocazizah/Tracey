#ifndef TRACEY_LEXER_HXX
#define TRACEY_LEXER_HXX

#include <cstddef>
#include <cstdio>
#include <optional>
#include <string_view>

namespace tracey
{

/* Token types */
enum class token_type
{
    eof = 0,
    identifier,
    integer,
    floating,
    string,
    keyword_int,
    keyword_float,
    keyword_double,
    keyword_return,
    keyword_void,
    keyword_bool,
    keyword_char,
    keyword_true,
    keyword_false,
    keyword_if,
    keyword_else,
    keyword_while,
    lparen,
    rparen,
    lbrace,
    rbrace,
    lbracket,
    rbracket,
    semicolon,
    comma,
    dot,
    assign,
    plus,
    minus,
    star,
    ampersand,
    slash,
    percent,
    bang,
    less,
    greater,
    less_equal,
    greater_equal,
    equal_equal,
    bang_equal,
    amp_amp,
    pipe_pipe,
    error_,
};

/* Error kinds carried by token_type::error_ tokens.
 *
 * The lexer never stops at the first error: it reports an error token and
 * keeps scanning, so a single run can surface every lexical problem. Error
 * tokens always consume at least one character, which guarantees progress
 * and termination. */
enum class token_error
{
    none = 0,
    invalid_character,
    invalid_number_suffix,
    invalid_escape,
    unterminated_string,
    unterminated_comment,
};

/* Token structure.
 *
 * `lexeme` is a non-owning view into the source content that the producing
 * `lexer` was constructed from. It is only valid for as long as that content
 * stays alive (callers keep their `source` objects alive while lexing).
 *
 * `error` is only meaningful when `type == token_type::error_`. */
struct token
{
    token_type type{token_type::eof};
    token_error error{token_error::none};
    std::string_view lexeme{};
    std::size_t line{1};
    std::size_t column{1};

    /* True when this token reports a lexical error */
    [[nodiscard]] constexpr bool is_error() const noexcept
    {
        return type == token_type::error_;
    }

    /* True when this token is not an error */
    [[nodiscard]] constexpr bool valid() const noexcept
    {
        return !is_error();
    }

    /* Print token debug info */
    void print_debug(std::FILE *p_out = nullptr) const;
};

/* Token type to string */
[[nodiscard]] std::string_view to_string(token_type type) noexcept;

/* Lexer error kind to string (debug form, e.g. "INVALID_CHARACTER") */
[[nodiscard]] std::string_view to_string(token_error error) noexcept;

/* Print a one-line user-facing diagnostic for a lexer error token:
 *
 *     Error: path:line:column: message
 *
 * Bytes in the token's lexeme that are not printable ASCII are rendered as
 * escapes (e.g. \n, \x1F) so the message stays readable. */
void print_error(const token &t, std::string_view path, std::FILE *p_out = nullptr);

/*
 * Movable, value-semantic cursor over a source string view.
 *
 * Technical notes:
 * - Positions are snapshotted before consuming bytes, so lookup is O(1).
 * - peek caches at most one token; next consumes cache first.
 * - Every token consumes at least one byte to guarantee progress.
 * - Error recovery allows reporting multiple lexical issues in one pass.
 */
class lexer
{
  public:
    explicit lexer(std::string_view content) noexcept;

    /* Get the next token */
    [[nodiscard]] token next();

    /* Peek at the next token without consuming it */
    [[nodiscard]] token peek();

    /* Check whether every character has been consumed.
     *
     * Note: this reports "no input left", not "the next token is EOF". After
     * lexing the final token of an input that does not end in whitespace it
     * is already true while the EOF token still has to be produced. */
    [[nodiscard]] bool is_at_end() const noexcept;

    /* Get current line number */
    [[nodiscard]] std::size_t line() const noexcept;

    /* Get current column number */
    [[nodiscard]] std::size_t column() const noexcept;

    /* Print lexer debug info */
    void print_debug(std::FILE *p_out = nullptr) const;

  private:
    /* Internal helpers */
    [[nodiscard]] bool at_end() const noexcept;
    [[nodiscard]] char current() const noexcept;
    [[nodiscard]] char current_next() const noexcept;
    char advance() noexcept;
    bool match(char expected) noexcept;

    /* Trivia skipping */
    void skip_whitespace() noexcept;
    void skip_line_comment() noexcept;
    [[nodiscard]] bool skip_block_comment() noexcept;

    [[nodiscard]] token scan_token();
    [[nodiscard]] token scan_identifier(std::size_t start_pos, std::size_t start_line,
                                        std::size_t start_column);
    [[nodiscard]] token scan_number(std::size_t start_pos, std::size_t start_line,
                                    std::size_t start_column);
    [[nodiscard]] token scan_string(std::size_t start_pos, std::size_t start_line,
                                    std::size_t start_column);

    [[nodiscard]] token make_token(token_type type, std::size_t start_pos, std::size_t length,
                                   std::size_t line, std::size_t column) const noexcept;
    [[nodiscard]] token make_error_token(token_error error, std::size_t start_pos,
                                         std::size_t length, std::size_t line,
                                         std::size_t column) const noexcept;

    [[nodiscard]] static token_type check_keyword(std::string_view text) noexcept;

    std::string_view content_;
    std::size_t position_{0};
    std::size_t line_{1};
    std::size_t column_{1};
    std::optional<token> peeked_{};
};

} // namespace tracey

#endif /* TRACEY_LEXER_HXX */
