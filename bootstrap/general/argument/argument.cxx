#include <tracey/argument.hxx>

#include <iostream>
#include <string>
#include <string_view>

namespace tracey::argument {

std::expected<void, std::string> argument_option::parse_args(
    std::span<const char* const> args) noexcept
{
    // Reset state in case parse_args is called multiple times.
    input_file_.clear();
    output_file_.reset();
    warning_all_ = false;
    warning_extra_ = false;

    for (std::size_t i = 0; i < args.size(); ++i) {
        std::string_view arg = args[i];

        if (arg == "-o" || arg == "--output") {
            if (i + 1 >= args.size()) {
                return std::unexpected("Missing value for option '" + std::string(arg) + "'");
            }
            output_file_ = std::string(args[++i]);
        } else if (arg == "-Wall" || arg == "--Wall") {
            warning_all_ = true;
        } else if (arg == "-Wextra" || arg == "--Wextra") {
            warning_extra_ = true;
        } else if (arg.starts_with('-')) {
            return std::unexpected("Unknown option: " + std::string(arg));
        } else {
            // Positional argument: input file.
            if (!input_file_.empty()) {
                return std::unexpected("Multiple input files specified");
            }
            input_file_ = std::string(arg);
        }
    }

    if (input_file_.empty()) {
        return std::unexpected("No input file specified");
    }

    return {}; // success
}

void argument_option::debug_print_args() const
{
    std::cerr << "Input file: " << input_file_ << '\n';
    if (output_file_) {
        std::cerr << "Output file: " << *output_file_ << '\n';
    } else {
        std::cerr << "Output file: (none)\n";
    }
    std::cerr << "Warning all: " << (warning_all_ ? "yes" : "no") << '\n';
    std::cerr << "Warning extra: " << (warning_extra_ ? "yes" : "no") << '\n';
}

const std::string& argument_option::get_input_file() const noexcept
{
    return input_file_;
}

const std::optional<std::string>& argument_option::get_output_file() const noexcept
{
    return output_file_;
}

bool argument_option::get_warning_all() const noexcept
{
    return warning_all_;
}

bool argument_option::get_warning_extra() const noexcept
{
    return warning_extra_;
}

} // namespace tracey::argument
