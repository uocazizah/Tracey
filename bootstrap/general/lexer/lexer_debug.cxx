#include <tracey/lexer.hxx>

#include <print>
#include <string_view>

namespace {

/** Render one byte for a diagnostic: printable ASCII as-is, common control
 * characters as their usual two-character escape, everything else as \xNN. */
void print_byte_escaped(std::FILE* out, char c)
{
    switch (c) {
        case '\n': std::fputs("\\n", out);  return;
        case '\t': std::fputs("\\t", out);  return;
        case '\r': std::fputs("\\r", out);  return;
        case '\v': std::fputs("\\v", out);  return;
        case '\f': std::fputs("\\f", out);  return;
        case '\\': std::fputs("\\\\", out); return;
        default: break;
    }

    const auto byte = static_cast<unsigned char>(c);
    if (byte >= 0x20 && byte < 0x7F) {
        std::fputc(byte, out);
    } else {
        std::print(out, "\\x{:02X}", byte);
    }
}

void print_lexeme_escaped(std::FILE* out, std::string_view lexeme)
{
    for (const char c : lexeme) {
        print_byte_escaped(out, c);
    }
}

} /** anonymous namespace */

namespace tracey {

void token::print_debug(std::FILE* out) const
{
    if (out == nullptr) out = stdout;

    std::print(out, "Token:\n");
    std::print(out, "  Type: {}\n", to_string(type));
    if (type == token_type::error_) {
        std::print(out, "  Error: {}\n", to_string(error));
    }
    std::print(out, "  Line: {}\n", line);
    std::print(out, "  Column: {}\n", column);
    std::print(out, "  Length: {}\n", lexeme.size());
    if (!lexeme.empty()) {
        std::fputs("  Text: ", out);
        std::fwrite(lexeme.data(), 1, lexeme.size(), out);
        std::fputs("\n", out);
    }
}

void lexer::print_debug(std::FILE* out) const
{
    if (out == nullptr) out = stdout;

    std::print(out, "Lexer:\n");
    std::print(out, "  Position: {} / {}\n", position_, content_.size());
    std::print(out, "  Line: {}\n", line_);
    std::print(out, "  Column: {}\n", column_);
    std::print(out, "  Has peeked: {}\n", peeked_ ? "true" : "false");

    if (peeked_) {
        std::print(out, "  Peeked token:\n");
        peeked_->print_debug(out);
    }

    /** Print remaining content preview */
    if (position_ < content_.size()) {
        const std::size_t remaining = content_.size() - position_;
        const std::size_t preview_len = remaining < 80 ? remaining : 80;
        std::print(out, "  Remaining content preview:\n");
        std::fputs("  \"", out);
        for (std::size_t i = 0; i < preview_len; i++) {
            const char c = content_[position_ + i];
            if (c == '\n') {
                std::fputs("\\n", out);
            } else if (c == '\t') {
                std::fputs("\\t", out);
            } else if (c == '\r') {
                std::fputs("\\r", out);
            } else {
                std::fputc(static_cast<unsigned char>(c), out);
            }
        }
        if (remaining > 80) std::fputs("...", out);
        std::fputs("\"\n", out);
    }
}

void print_error(const token& t, std::string_view path, std::FILE* out)
{
    if (out == nullptr) out = stderr;

    std::print(out, "Error: {}:{}:{}: ", path, t.line, t.column);

#if defined(__clang__)
    #pragma clang diagnostic push
    #pragma clang diagnostic ignored "-Wcovered-switch-default"
#endif
    switch (t.error) {
        case token_error::invalid_character:
            std::fputs("unexpected character '", out);
            print_lexeme_escaped(out, t.lexeme);
            std::fputs("'\n", out);
            return;

        case token_error::invalid_number_suffix:
            std::fputs("invalid suffix on integer literal '", out);
            print_lexeme_escaped(out, t.lexeme);
            std::fputs("'\n", out);
            return;

        case token_error::invalid_escape:
            std::fputs("invalid escape sequence '", out);
            print_lexeme_escaped(out, t.lexeme);
            std::fputs("'\n", out);
            return;

        case token_error::unterminated_string:
            std::fputs("unterminated string literal\n", out);
            return;

        case token_error::unterminated_comment:
            std::fputs("unterminated block comment (missing '*/')\n", out);
            return;

        case token_error::none:
        default:
            std::fputs("lexer error\n", out);
            return;
    }
#if defined(__clang__)
    #pragma clang diagnostic pop
#endif
}

} /** namespace tracey */
