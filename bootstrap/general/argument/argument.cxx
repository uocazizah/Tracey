#include <tracey/argument.hxx>

namespace tracey {

std::optional<std::string_view> args::output() const noexcept
{
    if (output_) return std::string_view{*output_};
    return std::nullopt;
}

std::optional<std::string_view> args::find_define(std::string_view name) const noexcept
{
    if (name.empty()) return std::nullopt;
    for (const auto& def : defines_) {
        /* Match if the define is exactly `name` or `name=value`. */
        if (def == name) return std::string_view{def};
        if (def.starts_with(name) && def.size() > name.size() && def[name.size()] == '=') {
            return std::string_view{def};
        }
    }
    return std::nullopt;
}

bool args::has_include_path(std::string_view path) const noexcept
{
    for (const auto& p : include_paths_) {
        if (p == path) return true;
    }
    return false;
}

std::string_view to_string(args_error e) noexcept
{
#if defined(__clang__)
    #pragma clang diagnostic push
    #pragma clang diagnostic ignored "-Wcovered-switch-default"
#endif
    switch (e) {
        case args_error::nomem:       return "Out of memory";
        case args_error::invalid:     return "Invalid argument";
        case args_error::too_many:    return "Too many arguments";
        case args_error::missing_arg: return "Missing required argument";
        case args_error::empty_arg:   return "Empty argument not allowed";
        default:                      return "Unknown error";
    }
#if defined(__clang__)
    #pragma clang diagnostic pop
#endif
}

} /* namespace tracey */
