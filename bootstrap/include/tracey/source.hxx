#ifndef TRACEY_SOURCE_HXX
#define TRACEY_SOURCE_HXX

#include <cstddef>
#include <cstdio>
#include <expected>
#include <filesystem>
#include <string>
#include <string_view>

namespace tracey {

/* Error codes for source::read */
enum class source_error {
    invalid_path,
    open_failed,
    not_regular_file,
    too_large,
    read_failed,
    no_memory,
};

/* An owned, immutable snapshot of a source file loaded into memory. */
class source {
public:
    /* Read a source file from disk. Hardened on POSIX (O_NOFOLLOW + fstat +
     * S_ISREG); portable std::filesystem/ifstream fallback elsewhere. */
    [[nodiscard]] static std::expected<source, source_error>
    read(const std::filesystem::path& path);

    [[nodiscard]] const std::filesystem::path& path() const noexcept { return path_; }
    [[nodiscard]] std::string_view content() const noexcept { return content_; }
    [[nodiscard]] std::size_t size() const noexcept { return content_.size(); }
    [[nodiscard]] bool empty() const noexcept { return content_.empty(); }

    /* Print source debug info */
    void print_debug(std::FILE* out = nullptr) const;

private:
    source(std::filesystem::path path, std::string content)
        : path_{std::move(path)}, content_{std::move(content)}
    {
    }

#if defined(__unix__)
    /* Hardened POSIX read path (see source.cxx for the security rationale). */
    [[nodiscard]] static std::expected<source, source_error>
    read_posix(const std::filesystem::path& path);
#endif

    /* Portable std::filesystem/ifstream fallback (Windows and others). */
    [[nodiscard]] static std::expected<source, source_error>
    read_portable(const std::filesystem::path& path);

    std::filesystem::path path_;
    std::string content_;
};

/* Human-readable message for a source error */
[[nodiscard]] std::string_view to_string(source_error e) noexcept;

} /* namespace tracey */

#endif /* TRACEY_SOURCE_HXX */
