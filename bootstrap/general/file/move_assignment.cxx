#include <tracey/file/file.hxx>
#include <utility>

tracey::file::TraceyFile& tracey::file::TraceyFile::operator=(TraceyFile&& other) noexcept {
    if (this != &other) {
        close();
        handle_ = std::exchange(other.handle_, nullptr);
        path_ = std::move(other.path_);
        buffer_ = std::move(other.buffer_);
    }
    return *this;
}
