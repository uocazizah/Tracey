#include <tracey/lexer.hxx>

#include <tracey/config.hxx>

#include <array>

namespace
{

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

[[nodiscard]] constexpr bool is_whitespace(char c) noexcept
{
    return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\v' || c == '\f';
}

/* Escape sequences accepted inside string literals. */
[[nodiscard]] constexpr bool is_valid_escape(char c) noexcept
{
    return c == 'n' || c == 't' || c == 'r' || c == '\\' || c == '"' || c == '0';
}

struct keyword_entry
{
    std::string_view text;
    tracey::token_type type;
};

/* Keywords table. Entries are kept sorted by `text`: the lookup below is a
 * linear scan (fine at this size) and staying sorted makes a future binary
 * search or switch-based dispatch mechanical to introduce. */
constexpr std::array<keyword_entry, 12> keywords{{
    {"bool", tracey::token_type::keyword_bool},
    {"char", tracey::token_type::keyword_char},
    {"double", tracey::token_type::keyword_double},
    {"else", tracey::token_type::keyword_else},
    {"false", tracey::token_type::keyword_false},
    {"float", tracey::token_type::keyword_float},
    {"if", tracey::token_type::keyword_if},
    {"int", tracey::token_type::keyword_int},
    {"return", tracey::token_type::keyword_return},
    {"true", tracey::token_type::keyword_true},
    {"void", tracey::token_type::keyword_void},
    {"while", tracey::token_type::keyword_while},
}};

} // namespace

namespace tracey
{

lexer::lexer(std::string_view content) noexcept : content_{content}
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
    if (peeked_)
    {
        token t = std::move(*peeked_);
        peeked_.reset();
        return t;
    }

    return scan_token();
}

token lexer::peek()
{
    if (!peeked_)
    {
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
    if (at_end())
        return '\0';
    return content_[position_];
}

char lexer::current_next() const noexcept
{
    if (position_ + 1 >= content_.size())
        return '\0';
    return content_[position_ + 1];
}

char lexer::advance() noexcept
{
    if (at_end())
        return '\0';

    const char c = content_[position_];
    position_++;

    if (c == '\n')
    {
        line_++;
        column_ = 1;
    }
    else
    {
        column_++;
    }

    return c;
}

bool lexer::match(char expected) noexcept
{
    if (at_end())
        return false;
    if (content_[position_] != expected)
        return false;

    advance();
    return true;
}

void lexer::skip_whitespace() noexcept
{
    while (!at_end())
    {
        if (is_whitespace(current()))
        {
            advance();
        }
        else
        {
            break;
        }
    }
}

void lexer::skip_line_comment() noexcept
{
    /* The leading `//` is still unconsumed. Comments are trivia: skipped
     * without producing a token, but with line/column kept accurate. */
    advance(); /* '/' */
    advance(); /* '/' */

    while (!at_end() && current() != '\n')
    {
        advance();
    }
}

bool lexer::skip_block_comment() noexcept
{
    /* The leading slash-star is still unconsumed. Consumes through the
     * matching star-slash. Returns false when the input ends before the
     * comment closes (the caller turns that into an error token anchored
     * at the opening slash-star, so the diagnostic points at the comment,
     * not at EOF). */
    advance(); /* '/' */
    advance(); /* '*' */

    while (!at_end())
    {
        if (current() == '*' && current_next() == '/')
        {
            advance(); /* '*' */
            advance(); /* '/' */
            return true;
        }
        advance();
    }

    return false;
}

token lexer::scan_token()
{
    /* Skip trivia (whitespace and comments) in front of the next token. */
    for (;;)
    {
        skip_whitespace();
        if (at_end())
            break;

        if (current() == '/' && current_next() == '/')
        {
            skip_line_comment();
            continue;
        }
        if (current() == '/' && current_next() == '*')
        {
            const std::size_t start_pos = position_;
            const std::size_t start_line = line_;
            const std::size_t start_column = column_;

            if (!skip_block_comment())
            {
                return make_error_token(token_error::unterminated_comment, start_pos,
                                        position_ - start_pos, start_line, start_column);
            }
            continue;
        }
        break;
    }

    /* Capture the token's start position *before* consuming the first
     * character: `advance()` mutates line_/column_, so we snapshot them
     * here and thread them through to make_token(). This keeps line/column
     * tracking O(1) per token instead of rescanning the source. */
    const std::size_t start_pos = position_;
    const std::size_t start_line = line_;
    const std::size_t start_column = column_;

    if (at_end())
    {
        return make_token(token_type::eof, start_pos, 0, start_line, start_column);
    }

    const char c = advance();

    switch (c)
    {
        case '(':
            return make_token(token_type::lparen, start_pos, 1, start_line, start_column);
        case ')':
            return make_token(token_type::rparen, start_pos, 1, start_line, start_column);
        case '{':
            return make_token(token_type::lbrace, start_pos, 1, start_line, start_column);
        case '}':
            return make_token(token_type::rbrace, start_pos, 1, start_line, start_column);
        case '[':
            return make_token(token_type::lbracket, start_pos, 1, start_line, start_column);
        case ']':
            return make_token(token_type::rbracket, start_pos, 1, start_line, start_column);
        case ';':
            return make_token(token_type::semicolon, start_pos, 1, start_line, start_column);
        case ',':
            return make_token(token_type::comma, start_pos, 1, start_line, start_column);
        case '.':
            return make_token(token_type::dot, start_pos, 1, start_line, start_column);
        case '+':
            return make_token(token_type::plus, start_pos, 1, start_line, start_column);
        case '-':
            return make_token(token_type::minus, start_pos, 1, start_line, start_column);
        case '*':
            return make_token(token_type::star, start_pos, 1, start_line, start_column);
        case '%':
            return make_token(token_type::percent, start_pos, 1, start_line, start_column);
        case '/':
            /* Line and block comment openers were consumed as trivia
             * above, so a slash here is the division operator. */
            return make_token(token_type::slash, start_pos, 1, start_line, start_column);

        case '=':
            return make_token(match('=') ? token_type::equal_equal : token_type::assign, start_pos,
                              position_ - start_pos, start_line, start_column);
        case '!':
            return make_token(match('=') ? token_type::bang_equal : token_type::bang, start_pos,
                              position_ - start_pos, start_line, start_column);
        case '<':
            return make_token(match('=') ? token_type::less_equal : token_type::less, start_pos,
                              position_ - start_pos, start_line, start_column);
        case '>':
            return make_token(match('=') ? token_type::greater_equal : token_type::greater,
                              start_pos, position_ - start_pos, start_line, start_column);
        case '&':
            if (match('&'))
            {
                return make_token(token_type::amp_amp, start_pos, position_ - start_pos, start_line,
                                  start_column);
            }
            return make_token(token_type::ampersand, start_pos, 1, start_line, start_column);
        case '|':
            if (match('|'))
            {
                return make_token(token_type::pipe_pipe, start_pos, position_ - start_pos,
                                  start_line, start_column);
            }
            break; /* a lone '|' is not a token (yet) */

        case '"':
            return scan_string(start_pos, start_line, start_column);

        default:
            if (is_digit(c))
            {
                return scan_number(start_pos, start_line, start_column);
            }
            if (is_ident_start(c))
            {
                return scan_identifier(start_pos, start_line, start_column);
            }
            break;
    }

    TRACEY_TRACE("[lexer] unexpected character '{}' at {}:{}\n", c, start_line, start_column);
    return make_error_token(token_error::invalid_character, start_pos, position_ - start_pos,
                            start_line, start_column);
}

token lexer::scan_identifier(std::size_t start_pos, std::size_t start_line,
                             std::size_t start_column)
{
    while (!at_end())
    {
        const char c = current();
        if (is_ident_cont(c))
        {
            advance();
        }
        else
        {
            break;
        }
    }

    const std::size_t length = position_ - start_pos;
    const token_type type = check_keyword(content_.substr(start_pos, length));

    return make_token(type, start_pos, length, start_line, start_column);
}

token lexer::scan_number(std::size_t start_pos, std::size_t start_line, std::size_t start_column)
{
    bool is_float = false;

    while (!at_end() && is_digit(current()))
    {
        advance();
    }

    /* Fractional part: `1.0`, `1.`. */
    if (!at_end() && current() == '.')
    {
        is_float = true;
        advance();
        while (!at_end() && is_digit(current()))
        {
            advance();
        }
    }

    /* Exponent part: `1e3`, `1.0E-2`. */
    if (!at_end() && (current() == 'e' || current() == 'E'))
    {
        is_float = true;
        advance();
        if (!at_end() && (current() == '+' || current() == '-'))
        {
            advance();
        }
        while (!at_end() && is_digit(current()))
        {
            advance();
        }
    }

    /* Optional float suffix: `1.0f`. */
    if (!at_end() && (current() == 'f' || current() == 'F'))
    {
        is_float = true;
        advance();
    }

    /* A number immediately followed by an identifier character (e.g.
     * `123abc`, `0x10`) is not a valid token. Consume the whole run so the
     * diagnostic can show it, and report an error instead of silently
     * splitting into `123` + `abc`. */
    if (!at_end() && is_ident_start(current()))
    {
        while (!at_end() && is_ident_cont(current()))
        {
            advance();
        }

        TRACEY_TRACE("[lexer] invalid number suffix at {}:{}\n", start_line, start_column);
        return make_error_token(token_error::invalid_number_suffix, start_pos,
                                position_ - start_pos, start_line, start_column);
    }

    const std::size_t length = position_ - start_pos;
    if (is_float)
    {
        return make_token(token_type::floating, start_pos, length, start_line, start_column);
    }
    return make_token(token_type::integer, start_pos, length, start_line, start_column);
}

token lexer::scan_string(std::size_t start_pos, std::size_t start_line, std::size_t start_column)
{
    /* The opening quote was already consumed. Strings do not span lines: an
     * unescaped newline (or end of input) before the closing quote is an
     * unterminated string.
     *
     * An invalid escape sequence does not stop the scan: the string is
     * consumed to its closing quote (so the rest of the file still lexes
     * normally) and a single error token is reported at the offending
     * backslash. */
    std::size_t bad_escape_pos = 0;
    std::size_t bad_escape_line = 0;
    std::size_t bad_escape_column = 0;
    bool has_bad_escape = false;

    while (!at_end())
    {
        const char c = current();

        if (c == '"')
        {
            advance(); /* closing quote */

            if (has_bad_escape)
            {
                return make_error_token(token_error::invalid_escape, bad_escape_pos, 2,
                                        bad_escape_line, bad_escape_column);
            }
            return make_token(token_type::string, start_pos, position_ - start_pos, start_line,
                              start_column);
        }

        if (c == '\n')
            break; /* unterminated: stop before the newline */

        if (c == '\\')
        {
            const std::size_t escape_pos = position_;
            const std::size_t escape_line = line_;
            const std::size_t escape_column = column_;
            advance(); /* backslash */

            if (at_end() || current() == '\n')
                break; /* unterminated */

            if (!has_bad_escape && !is_valid_escape(current()))
            {
                has_bad_escape = true;
                bad_escape_pos = escape_pos;
                bad_escape_line = escape_line;
                bad_escape_column = escape_column;
            }
            advance(); /* escaped character */
            continue;
        }

        advance();
    }

    /* Stop before the newline (or at end of input) so the next scan still
     * sees it; point the error at the opening quote. */
    return make_error_token(token_error::unterminated_string, start_pos, position_ - start_pos,
                            start_line, start_column);
}

token_type lexer::check_keyword(std::string_view text) noexcept
{
    for (const auto &kw : keywords)
    {
        if (kw.text == text)
        {
            return kw.type;
        }
    }
    return token_type::identifier;
}

token lexer::make_token(token_type type, std::size_t start_pos, std::size_t length,
                        std::size_t line, std::size_t column) const noexcept
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

token lexer::make_error_token(token_error error, std::size_t start_pos, std::size_t length,
                              std::size_t line, std::size_t column) const noexcept
{
    token t = make_token(token_type::error_, start_pos, length, line, column);
    t.error = error;
    return t;
}

std::string_view to_string(token_type type) noexcept
{
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wcovered-switch-default"
#endif
    switch (type)
    {
        case token_type::eof:
            return "EOF";
        case token_type::identifier:
            return "IDENTIFIER";
        case token_type::integer:
            return "INTEGER";
        case token_type::floating:
            return "FLOATING";
        case token_type::string:
            return "STRING";
        case token_type::keyword_int:
            return "KEYWORD_INT";
        case token_type::keyword_return:
            return "KEYWORD_RETURN";
        case token_type::keyword_void:
            return "KEYWORD_VOID";
        case token_type::keyword_bool:
            return "KEYWORD_BOOL";
        case token_type::keyword_char:
            return "KEYWORD_CHAR";
        case token_type::keyword_float:
            return "KEYWORD_FLOAT";
        case token_type::keyword_double:
            return "KEYWORD_DOUBLE";
        case token_type::keyword_true:
            return "KEYWORD_TRUE";
        case token_type::keyword_false:
            return "KEYWORD_FALSE";
        case token_type::keyword_if:
            return "KEYWORD_IF";
        case token_type::keyword_else:
            return "KEYWORD_ELSE";
        case token_type::keyword_while:
            return "KEYWORD_WHILE";
        case token_type::lparen:
            return "LPAREN";
        case token_type::rparen:
            return "RPAREN";
        case token_type::lbrace:
            return "LBRACE";
        case token_type::rbrace:
            return "RBRACE";
        case token_type::lbracket:
            return "LBRACKET";
        case token_type::rbracket:
            return "RBRACKET";
        case token_type::semicolon:
            return "SEMICOLON";
        case token_type::comma:
            return "COMMA";
        case token_type::dot:
            return "DOT";
        case token_type::assign:
            return "ASSIGN";
        case token_type::plus:
            return "PLUS";
        case token_type::minus:
            return "MINUS";
        case token_type::star:
            return "STAR";
        case token_type::slash:
            return "SLASH";
        case token_type::percent:
            return "PERCENT";
        case token_type::bang:
            return "BANG";
        case token_type::less:
            return "LESS";
        case token_type::greater:
            return "GREATER";
        case token_type::less_equal:
            return "LESS_EQUAL";
        case token_type::greater_equal:
            return "GREATER_EQUAL";
        case token_type::equal_equal:
            return "EQUAL_EQUAL";
        case token_type::bang_equal:
            return "BANG_EQUAL";
        case token_type::amp_amp:
            return "AMP_AMP";
        case token_type::ampersand:
            return "AMPERSAND";
        case token_type::pipe_pipe:
            return "PIPE_PIPE";
        case token_type::error_:
            return "ERROR";
        default:
            return "UNKNOWN";
    }
#if defined(__clang__)
#pragma clang diagnostic pop
#endif
}

std::string_view to_string(token_error error) noexcept
{
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wcovered-switch-default"
#endif
    switch (error)
    {
        case token_error::none:
            return "NONE";
        case token_error::invalid_character:
            return "INVALID_CHARACTER";
        case token_error::invalid_number_suffix:
            return "INVALID_NUMBER_SUFFIX";
        case token_error::invalid_escape:
            return "INVALID_ESCAPE";
        case token_error::unterminated_string:
            return "UNTERMINATED_STRING";
        case token_error::unterminated_comment:
            return "UNTERMINATED_COMMENT";
        default:
            return "UNKNOWN";
    }
#if defined(__clang__)
#pragma clang diagnostic pop
#endif
}

} // namespace tracey
