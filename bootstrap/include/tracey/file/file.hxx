#ifndef TRACEY_FILE_HXX
#define TRACEY_FILE_HXX

#include <cstdio>
#include <string>
#include <vector>

namespace tracey::file
{

class TraceyFile {
public:
    TraceyFile() noexcept;
    ~TraceyFile();

    TraceyFile(const TraceyFile&) = delete;
    TraceyFile& operator=(const TraceyFile&) = delete;
    TraceyFile(TraceyFile&& other) noexcept;
    TraceyFile& operator=(TraceyFile&& other) noexcept;

    bool open(const char* path);

    void close();

    void debug_print() const;

    [[nodiscard]] const std::string& path() const noexcept { return path_; }
    [[nodiscard]] const std::vector<char>& buffer() const noexcept { return buffer_; }

private:
    std::FILE* handle_ = nullptr;
    std::string path_;
    std::vector<char> buffer_;
};

}

#endif // TRACEY_FILE_HXX
