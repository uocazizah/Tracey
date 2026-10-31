#include <tracey/lexer.hxx>

#include <print>
#include <string_view>
#include <unistd.h>

namespace
{

/* Render one byte for a diagnostic: printable ASCII as-is, common control
 * characters as their usual two-character escape, everything else as \xNN. */
void print_byte_escaped(std::FILE *p_out, char c)
{
    switch (c)
    {
        case '\n':
            std::fputs("\\n", p_out);
            return;
        case '\t':
            std::fputs("\\t", p_out);
            return;
        case '\r':
            std::fputs("\\r", p_out);
            return;
        case '\v':
            std::fputs("\\v", p_out);
            return;
        case '\f':
            std::fputs("\\f", p_out);
            return;
        case '\\':
            std::fputs("\\\\", p_out);
            return;
        default:
            break;
    }

    const auto byte = static_cast<unsigned char>(c);
    if (byte >= 0x20 && byte < 0x7F)
    {
        std::fputc(byte, p_out);
    }
    else
    {
        std::print(p_out, "\\x{:02X}", byte);
    }
}

void print_lexeme_escaped(std::FILE *p_out, std::string_view lexeme)
{
    for (const char c : lexeme)
    {
        print_byte_escaped(p_out, c);
    }
}

} // namespace

namespace tracey
{

void token::print_debug(std::FILE *p_out) const
{
    if (p_out == nullptr)
        p_out = stdout;

    std::print(p_out, "Token:\n");
    std::print(p_out, "  Type: {}\n", to_string(type));
    if (type == token_type::error_)
    {
        std::print(p_out, "  Error: {}\n", to_string(error));
    }
    std::print(p_out, "  Line: {}\n", line);
    std::print(p_out, "  Column: {}\n", column);
    std::print(p_out, "  Length: {}\n", lexeme.size());
    if (!lexeme.empty())
    {
        std::fputs("  Text: ", p_out);
        std::fwrite(lexeme.data(), 1, lexeme.size(), p_out);
        std::fputs("\n", p_out);
    }
}

void lexer::print_debug(std::FILE *p_out) const
{
    if (p_out == nullptr)
        p_out = stdout;

    std::print(p_out, "Lexer:\n");
    std::print(p_out, "  Position: {} / {}\n", position_, content_.size());
    std::print(p_out, "  Line: {}\n", line_);
    std::print(p_out, "  Column: {}\n", column_);
    std::print(p_out, "  Has peeked: {}\n", peeked_ ? "true" : "false");

    if (peeked_)
    {
        std::print(p_out, "  Peeked token:\n");
        peeked_->print_debug(p_out);
    }

    /* Print remaining content preview */
    if (position_ < content_.size())
    {
        const std::size_t remaining = content_.size() - position_;
        const std::size_t preview_len = remaining < 80 ? remaining : 80;
        std::print(p_out, "  Remaining content preview:\n");
        std::fputs("  \"", p_out);
        for (std::size_t i = 0; i < preview_len; i++)
        {
            const char c = content_[position_ + i];
            if (c == '\n')
            {
                std::fputs("\\n", p_out);
            }
            else if (c == '\t')
            {
                std::fputs("\\t", p_out);
            }
            else if (c == '\r')
            {
                std::fputs("\\r", p_out);
            }
            else
            {
                std::fputc(static_cast<unsigned char>(c), p_out);
            }
        }
        if (remaining > 80)
            std::fputs("...", p_out);
        std::fputs("\"\n", p_out);
    }
}

void print_error(const token &t, std::string_view path, std::FILE *p_out)
{
    if (p_out == nullptr)
        p_out = stderr;

    const bool color = isatty(fileno(p_out));
    std::print(p_out, "{}:{}:{}: ", path, t.line, t.column);
    if (color)
        std::fputs("\033[1;31m", p_out);
    std::fputs("error:", p_out);
    if (color)
        std::fputs("\033[0m", p_out);
    std::fputs(" ", p_out);

#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wcovered-switch-default"
#endif
    switch (t.error)
    {
        case token_error::invalid_character:
            std::fputs("unexpected character '", p_out);
            print_lexeme_escaped(p_out, t.lexeme);
            std::fputs("'\n", p_out);
            return;

        case token_error::invalid_number_suffix:
            std::fputs("invalid suffix on integer literal '", p_out);
            print_lexeme_escaped(p_out, t.lexeme);
            std::fputs("'\n", p_out);
            return;

        case token_error::invalid_escape:
            std::fputs("invalid escape sequence '", p_out);
            print_lexeme_escaped(p_out, t.lexeme);
            std::fputs("'\n", p_out);
            return;

        case token_error::unterminated_string:
            std::fputs("unterminated string literal\n", p_out);
            return;

        case token_error::unterminated_comment:
            std::fputs("unterminated block comment (missing '*/')\n", p_out);
            return;

        case token_error::none:
        default:
            std::fputs("lexer error\n", p_out);
            return;
    }
#if defined(__clang__)
#pragma clang diagnostic pop
#endif
}

} // namespace tracey
