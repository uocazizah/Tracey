#include <tracey/argument.hxx>

#include <print>
#include <string>

namespace {

/** Format a list of strings as "[a, b, c]" (parity with the original output). */
[[nodiscard]] std::string format_list(std::span<const std::string> items)
{
    std::string out{"["};
    for (std::size_t i = 0; i < items.size(); i++) {
        out += items[i];
        if (i + 1 < items.size()) out += ", ";
    }
    out += "]";
    return out;
}

[[nodiscard]] constexpr const char* bool_str(bool v) noexcept
{
    return v ? "true" : "false";
}

} /** anonymous namespace */

namespace tracey {

void args::print_debug(std::FILE* out) const
{
    if (out == nullptr) out = stdout;

    std::print(out, "Arguments:\n");
    std::print(out, "  Program: {}\n", program_name_);
    std::print(out, "  Version: {}\n", version_string_);

    std::print(out, "  Input files: {} (count={})\n", format_list(inputs_), inputs_.size());

    std::print(out, "  Output file: {}\n", output_ ? std::string_view{*output_} : std::string_view{"(none)"});

    std::print(out, "  Include paths: {} (count={})\n", format_list(include_paths_), include_paths_.size());

    std::print(out, "  Defines: {} (count={})\n", format_list(defines_), defines_.size());

    std::print(out, "  Optimization level: {}\n", static_cast<int>(optimization_));
    std::print(out, "  Warnings as errors: {}\n", bool_str(werror_));
    std::print(out, "  Debug info: {}\n", bool_str(debug_info_));
    std::print(out, "  Verbose: {}\n", bool_str(verbose_));
    std::print(out, "  Help: {}\n", bool_str(help_));
    std::print(out, "  Version: {}\n", bool_str(version_));
}

} /** namespace tracey */
