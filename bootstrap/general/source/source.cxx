#include <tracey/source.hxx>

#include <tracey/config.hxx>

#include <cstdint>
#include <fstream>
#include <limits>
#include <string>
#include <system_error>

#if defined(__unix__)
    #include <fcntl.h>
    #include <sys/stat.h>
    #include <unistd.h>
#endif

namespace {

/* Maximum accepted source size, enforced on every platform. Resolved from the
 * Kconfig symbol CONFIG_TRACEY_MAX_SOURCE_SIZE via tracey::config. */
constexpr std::size_t kMaxSourceSize = tracey::config::max_source_size;

#if defined(__unix__)
/* RAII wrapper for a raw file descriptor. */
struct fd_holder {
    int fd{-1};

    fd_holder() = default;
    explicit fd_holder(int f) noexcept : fd{f} {}
    ~fd_holder() { reset(); }

    fd_holder(const fd_holder&) = delete;
    fd_holder& operator=(const fd_holder&) = delete;

    void reset(int f = -1) noexcept
    {
        if (fd >= 0) ::close(fd);
        fd = f;
    }

    /* Give up ownership without closing (e.g. after handing the fd to
     * fdopen, which now owns it). */
    int release() noexcept
    {
        const int f = fd;
        fd = -1;
        return f;
    }

    [[nodiscard]] bool valid() const noexcept { return fd >= 0; }
};

/* RAII wrapper for a std::FILE* (closes the stream, which also closes the
 * underlying descriptor once ownership has been transferred). */
struct file_holder {
    std::FILE* fp{nullptr};

    file_holder() = default;
    explicit file_holder(std::FILE* f) noexcept : fp{f} {}
    ~file_holder() { reset(); }

    file_holder(const file_holder&) = delete;
    file_holder& operator=(const file_holder&) = delete;

    void reset(std::FILE* f = nullptr) noexcept
    {
        if (fp) std::fclose(fp);
        fp = f;
    }

    [[nodiscard]] bool valid() const noexcept { return fp != nullptr; }
};
#endif /* __unix__ */

} /* anonymous namespace */

namespace tracey {

std::expected<source, source_error> source::read(const std::filesystem::path& path)
{
#if defined(__unix__)
    return read_posix(path);
#else
    return read_portable(path);
#endif
}

#if defined(__unix__)
std::expected<source, source_error> source::read_posix(const std::filesystem::path& path)
{
    const std::string path_str = path.string();
    if (path_str.empty()) return std::unexpected(source_error::invalid_path);

    /* Open with O_NOFOLLOW to avoid symlink attacks (CWE-362), then fdopen.
     * O_NOFOLLOW is mandated by POSIX.1-2001, so there is deliberately NO
     * fallback that drops it: on EINVAL/EOPNOTSUPP/ENOTSUP we fail closed
     * instead of silently reopening the path without protection against
     * symlink redirection. (ELOOP still means "final component is a symlink"
     * and is also fatal.) The O_RDONLY|O_NOFOLLOW|O_CLOEXEC flags plus the
     * fstat() and S_ISREG() checks below are the accepted mitigation for this
     * open. */
    fd_holder fdh{::open(path_str.c_str(), O_RDONLY | O_NOFOLLOW | O_CLOEXEC)}; /* flawfinder: ignore */
    if (!fdh.valid()) return std::unexpected(source_error::open_failed);

    /* Use fstat on the fd to avoid TOCTOU race */
    struct stat st {};
    if (::fstat(fdh.fd, &st) != 0) return std::unexpected(source_error::open_failed);

    /* Ensure it's a regular file (not a device, pipe, etc.) */
    if (!S_ISREG(st.st_mode)) return std::unexpected(source_error::not_regular_file);

    /* Check for integer overflow - st.st_size is signed, SIZE_MAX is unsigned.
     * Reject a negative size or one that exceeds SIZE_MAX - 1. */
    if (st.st_size < 0) return std::unexpected(source_error::read_failed);
    if (static_cast<unsigned long long>(st.st_size) >
        static_cast<unsigned long long>(std::numeric_limits<std::size_t>::max()) - 1ULL) {
        return std::unexpected(source_error::read_failed);
    }

    const auto size = static_cast<std::size_t>(st.st_size);

    /* Enforce maximum source file size from Kconfig */
    if (size > kMaxSourceSize) return std::unexpected(source_error::too_large);

    /* Convert fd to FILE* for buffered reading. fdopen takes ownership of the
     * fd on success; on failure we still own it and must close it. */
    const int raw_fd = fdh.release();
    file_holder fh{::fdopen(raw_fd, "rb")};
    if (!fh.valid()) {
        ::close(raw_fd);
        return std::unexpected(source_error::open_failed);
    }

    std::string content;
    try {
        content.resize(size);
    } catch (...) {
        return std::unexpected(source_error::no_memory);
    }

    /* Read the whole file in a loop: fread() may return a short count on EOF
     * or error, so a single call cannot be trusted to fill the buffer
     * (CWE-120/CWE-20). Any shortfall or stream error is a hard failure - we
     * never treat a partially read source as valid. */
    std::size_t bytes_read = 0;
    while (bytes_read < size) {
        const std::size_t chunk =
            std::fread(content.data() + bytes_read, 1, size - bytes_read, fh.fp);
        if (chunk == 0) break;
        bytes_read += chunk;
    }
    if (std::ferror(fh.fp)) return std::unexpected(source_error::read_failed);
    if (bytes_read != size) return std::unexpected(source_error::read_failed);

    TRACEY_TRACE("[source] read '{}' ({} bytes)\n", path_str, size);

    return source{std::filesystem::path{path_str}, std::move(content)};
}
#endif /* __unix__ */

std::expected<source, source_error> source::read_portable(const std::filesystem::path& path)
{
    if (path.empty()) return std::unexpected(source_error::invalid_path);

    std::error_code ec;
    const auto file_size = std::filesystem::file_size(path, ec);
    if (ec) return std::unexpected(source_error::open_failed);
    if (file_size > static_cast<std::uintmax_t>(kMaxSourceSize)) {
        return std::unexpected(source_error::too_large);
    }

    std::ifstream in(path, std::ios::binary);
    if (!in) return std::unexpected(source_error::open_failed);

    std::string content;
    try {
        content.resize(static_cast<std::size_t>(file_size));
    } catch (...) {
        return std::unexpected(source_error::no_memory);
    }

    if (file_size > 0 &&
        !in.read(content.data(), static_cast<std::streamsize>(file_size))) {
        return std::unexpected(source_error::read_failed);
    }

    TRACEY_TRACE("[source] read '{}' ({} bytes, portable path)\n",
                 path.string(), file_size);

    return source{path, std::move(content)};
}

std::string_view to_string(source_error e) noexcept
{
#if defined(__clang__)
    #pragma clang diagnostic push
    #pragma clang diagnostic ignored "-Wcovered-switch-default"
#endif
    switch (e) {
        case source_error::invalid_path:      return "Invalid path";
        case source_error::open_failed:       return "Failed to open file";
        case source_error::not_regular_file:  return "Not a regular file";
        case source_error::too_large:         return "File exceeds maximum source size";
        case source_error::read_failed:       return "Failed to read file";
        case source_error::no_memory:         return "Out of memory";
        default:                              return "Unknown error";
    }
#if defined(__clang__)
    #pragma clang diagnostic pop
#endif
}

} /* namespace tracey */
