#include <tracey/argument.hxx>

#include <string_view>

namespace {

using tracey::args_error;
using tracey::opt_level;

/** Parse an option that requires a following argument (e.g. -o file, -I path).
 * Consumes the next argv slot via the by-reference index `i`. */
[[nodiscard]] std::expected<std::string_view, args_error>
parse_option_with_arg(int& i, int argc, char** argv)
{
    if (i + 1 >= argc) return std::unexpected(args_error::missing_arg);
    const char* val = argv[++i];
    if (val == nullptr || val[0] == '\0') return std::unexpected(args_error::empty_arg);
    return std::string_view{val};
}

/** Parse a single-digit optimization level '0'..'3' (exactly one character). */
[[nodiscard]] std::expected<opt_level, args_error>
parse_opt_level(std::string_view level)
{
    if (level.size() != 1 || level[0] < '0' || level[0] > '3') {
        return std::unexpected(args_error::invalid);
    }
    return static_cast<opt_level>(level[0] - '0');
}

/** Parse -O<level> or -O <level>. */
[[nodiscard]] std::expected<opt_level, args_error>
parse_optimization(std::string_view arg, int& i, int argc, char** argv)
{
    std::string_view level_str;
    if (arg.size() > 2) {
        level_str = arg.substr(2);
    } else {
        if (i + 1 >= argc) return std::unexpected(args_error::missing_arg);
        level_str = argv[++i];
    }
    return parse_opt_level(level_str);
}

} /** anonymous namespace */

namespace tracey {

std::expected<args, args_error> args::parse(int argc, char** argv, const args_config& config)
{
    if (argc < 0 || argv == nullptr) return std::unexpected(args_error::invalid);

    if (static_cast<std::size_t>(argc) > config.max_args) {
        return std::unexpected(args_error::too_many);
    }

    args a;
    a.program_name_ = config.program_name;
    a.version_string_ = config.version_string;
    a.description_ = config.description;

    for (int i = 1; i < argc; i++) {
        const char* carg = argv[i];
        if (!carg) continue;
        const std::string_view arg{carg};

        if (arg == "-h" || arg == "--help") {
            a.help_ = true;
            return a;
        }
        if (arg == "-v" || arg == "--version") {
            a.version_ = true;
            return a;
        }
        if (arg == "-V" || arg == "--verbose") {
            a.verbose_ = true;
        } else if (arg == "-Werror") {
            a.werror_ = true;
        } else if (arg == "-g") {
            a.debug_info_ = true;
        } else if (arg == "-S" || arg == "--assembly") {
            a.assembly_ = true;
        } else if (arg == "-o" || arg == "--output") {
            auto val = parse_option_with_arg(i, argc, argv);
            if (!val) return std::unexpected(val.error());
            a.output_ = std::string{*val};
        } else if (arg == "-I" || arg == "--include") {
            auto val = parse_option_with_arg(i, argc, argv);
            if (!val) return std::unexpected(val.error());
            a.include_paths_.emplace_back(*val);
        } else if (arg == "-D" || arg == "--define") {
            auto val = parse_option_with_arg(i, argc, argv);
            if (!val) return std::unexpected(val.error());
            a.defines_.emplace_back(*val);
        } else if (arg.starts_with("-O")) {
            auto level = parse_optimization(arg, i, argc, argv);
            if (!level) return std::unexpected(level.error());
            a.optimization_ = *level;
        } else if (arg.starts_with("--optimize")) {
            if (arg == "--optimize") {
                auto val = parse_option_with_arg(i, argc, argv);
                if (!val) return std::unexpected(val.error());
                auto level = parse_opt_level(*val);
                if (!level) return std::unexpected(level.error());
                a.optimization_ = *level;
            } else if (arg.starts_with("--optimize=")) {
                auto level = parse_opt_level(arg.substr(11));
                if (!level) return std::unexpected(level.error());
                a.optimization_ = *level;
            } else if (!config.allow_unknown_options) {
                /** Unknown --optimize* variant */
                return std::unexpected(args_error::invalid);
            }
        } else if (arg.starts_with('-')) {
            if (!config.allow_unknown_options) {
                /** Unknown option */
                return std::unexpected(args_error::invalid);
            }
        } else {
            /** Positional argument - input file */
            if (arg.empty()) return std::unexpected(args_error::empty_arg);
            a.inputs_.emplace_back(arg);
        }
    }

    return a;
}

} /** namespace tracey */
