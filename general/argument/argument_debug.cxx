#include <tracey/argument.hxx>

#include <print>
#include <string>

namespace
{

/* Format a list of strings as "[a, b, c]" (parity with the original output). */
[[nodiscard]] std::string format_list(std::span<const std::string> items)
{
    std::string out{"["};
    for (std::size_t i = 0; i < items.size(); i++)
    {
        out += items[i];
        if (i + 1 < items.size())
            out += ", ";
    }
    out += "]";
    return out;
}

[[nodiscard]] constexpr const char *bool_str(bool v) noexcept
{
    return v ? "true" : "false";
}

} // namespace

namespace tracey
{

void args::print_debug(std::FILE *p_out) const
{
    if (p_out == nullptr)
        p_out = stdout;

    std::print(p_out, "Arguments:\n");
    std::print(p_out, "  Program: {}\n", program_name_);
    std::print(p_out, "  Version: {}\n", version_string_);

    std::print(p_out, "  Input files: {} (count={})\n", format_list(inputs_), inputs_.size());

    std::print(p_out, "  Output file: {}\n",
               output_ ? std::string_view{*output_} : std::string_view{"(none)"});

    std::print(p_out, "  Include paths: {} (count={})\n", format_list(include_paths_),
               include_paths_.size());

    std::print(p_out, "  Defines: {} (count={})\n", format_list(defines_), defines_.size());

    std::print(p_out, "  Optimization level: {}\n", static_cast<int>(optimization_));
    std::print(p_out, "  Warnings as errors: {}\n", bool_str(werror_));
    std::print(p_out, "  Debug info: {}\n", bool_str(debug_info_));
    std::print(p_out, "  Verbose: {}\n", bool_str(verbose_));
    std::print(p_out, "  Help: {}\n", bool_str(help_));
    std::print(p_out, "  Version: {}\n", bool_str(version_));
}

} // namespace tracey
