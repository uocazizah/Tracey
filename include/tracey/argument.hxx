#ifndef TRACEY_ARGUMENT_HXX
#define TRACEY_ARGUMENT_HXX

#include <cstddef>
#include <cstdio>
#include <expected>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace tracey {

/** Optimization levels */
enum class opt_level : int {
    none = 0,
    level1 = 1,
    level2 = 2,
    level3 = 3,
};

/** Parse error codes */
enum class args_error {
    nomem,
    invalid,
    too_many,
    missing_arg,
    empty_arg,
};

/** Argument parser configuration */
struct args_config {
    std::string_view program_name{"tracey"};
    std::string_view version_string{"1.0.0"};
    std::string_view description{};
    std::size_t max_args{10000};
    bool allow_unknown_options{false};
};

/**
 * Command-line argument parser with value semantics and RAII ownership.
 *
 * Technical notes:
 * - parse returns expected to avoid exceptions in control flow.
 * - help/version short-circuit early for fast exit.
 * - max_args guards against excessive input counts.
 */
class args {
public:
    args() = default;

    /** Parse command line arguments */
    [[nodiscard]] static std::expected<args, args_error>
    parse(int argc, char** argv, const args_config& config = {});

    /** Accessors */
    [[nodiscard]] bool has_help() const noexcept { return help_; }
    [[nodiscard]] bool has_version() const noexcept { return version_; }
    [[nodiscard]] bool has_verbose() const noexcept { return verbose_; }
    [[nodiscard]] bool has_warnings_as_errors() const noexcept { return werror_; }
    [[nodiscard]] bool has_debug_info() const noexcept { return debug_info_; }
    [[nodiscard]] bool has_assembly() const noexcept { return assembly_; }

    [[nodiscard]] opt_level optimization() const noexcept { return optimization_; }
    [[nodiscard]] std::optional<std::string_view> output() const noexcept;

    [[nodiscard]] std::span<const std::string> inputs() const noexcept { return inputs_; }
    [[nodiscard]] std::span<const std::string> include_paths() const noexcept { return include_paths_; }
    [[nodiscard]] std::span<const std::string> defines() const noexcept { return defines_; }

    [[nodiscard]] std::optional<std::string_view>
    find_define(std::string_view name) const noexcept;

    [[nodiscard]] bool has_include_path(std::string_view path) const noexcept;

    /** Program identity (used by help/version/debug printing) */
    [[nodiscard]] std::string_view program_name() const noexcept { return program_name_; }
    [[nodiscard]] std::string_view version_string() const noexcept { return version_string_; }

    /** Debug output of the parsed state */
    void print_debug(std::FILE* out = nullptr) const;

private:
    std::vector<std::string> inputs_;
    std::vector<std::string> include_paths_;
    std::vector<std::string> defines_;
    std::optional<std::string> output_;

    std::string program_name_{"tracey"};
    std::string version_string_{"1.0.0"};
    std::string description_{};

    opt_level optimization_{opt_level::none};
    bool help_{false};
    bool version_{false};
    bool verbose_{false};
    bool werror_{false};
    bool debug_info_{false};
    bool assembly_{false};
};

/** Human-readable message for a parse error */
[[nodiscard]] std::string_view to_string(args_error e) noexcept;

/** Help and version printing (driven purely by the configuration) */
void print_help(const args_config& config, std::FILE* out = nullptr);
void print_version(const args_config& config, std::FILE* out = nullptr);

} /** namespace tracey */

#endif /** TRACEY_ARGUMENT_HXX */
