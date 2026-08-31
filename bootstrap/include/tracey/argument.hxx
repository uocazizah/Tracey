#ifndef ARGUMENT_HXX
#define ARGUMENT_HXX

#include <expected>
#include <optional>
#include <span>
#include <string>

namespace tracey::argument {

class argument_option
{
public:
    argument_option() = default;

    [[nodiscard]]
    std::expected<void, std::string> parse_args(std::span<const char* const> args) noexcept;

    void debug_print_args() const;

    [[nodiscard]]
    const std::string& get_input_file() const noexcept;

    [[nodiscard]]
    const std::optional<std::string>& get_output_file() const noexcept;

    [[nodiscard]]
    bool get_warning_all() const noexcept;

    [[nodiscard]]
    bool get_warning_extra() const noexcept;

private:
    std::string input_file_;
    std::optional<std::string> output_file_;
    bool warning_all_ = false;
    bool warning_extra_ = false;
};

} // namespace tracey::argument

#endif /* ARGUMENT_HXX */
