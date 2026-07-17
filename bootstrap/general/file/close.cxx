#include <tracey/file/file.hxx>
#include <cstdio>

void tracey::file::TraceyFile::close() {
    if (handle_) {
        std::fclose(handle_);
        handle_ = nullptr;
    }
    path_.clear();
    buffer_.clear();
    buffer_.shrink_to_fit();  // opsional
}
