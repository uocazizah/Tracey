#include <tracey/source.hxx>

#include <print>

namespace tracey
{

void source::print_debug(std::FILE *p_out) const
{
    if (p_out == nullptr)
        p_out = stdout;

    std::print(p_out, "Source:\n");
    std::print(p_out, "  Path: {}\n", path_.string());
    std::print(p_out, "  Size: {} bytes\n", size());
    std::print(p_out, "  Empty: {}\n", empty() ? "yes" : "no");

    if (!empty())
    {
        std::print(p_out, "  Content:\n");
        std::fputs("----\n", p_out);
        std::fwrite(content_.data(), 1, content_.size(), p_out);
        if (content_.back() != '\n')
            std::fputs("\n", p_out);
        std::fputs("----\n", p_out);
    }
}

} // namespace tracey
