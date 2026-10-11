#include <tracey/source.hxx>

#include <print>

namespace tracey {

void source::print_debug(std::FILE* out) const
{
    if (out == nullptr) out = stdout;

    std::print(out, "Source:\n");
    std::print(out, "  Path: {}\n", path_.string());
    std::print(out, "  Size: {} bytes\n", size());
    std::print(out, "  Empty: {}\n", empty() ? "yes" : "no");

    if (!empty()) {
        std::print(out, "  Content:\n");
        std::fputs("----\n", out);
        std::fwrite(content_.data(), 1, content_.size(), out);
        if (content_.back() != '\n') std::fputs("\n", out);
        std::fputs("----\n", out);
    }
}

} /* namespace tracey */
