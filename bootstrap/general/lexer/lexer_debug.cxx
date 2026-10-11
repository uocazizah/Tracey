#include <tracey/lexer.hxx>

#include <print>

namespace tracey {

void token::print_debug(std::FILE* out) const
{
    if (out == nullptr) out = stdout;

    std::print(out, "Token:\n");
    std::print(out, "  Type: {}\n", to_string(type));
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

    /* Print remaining content preview */
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

} /* namespace tracey */
