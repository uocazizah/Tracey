#include <tracey/file/file.hxx>
#include <iostream>

void tracey::file::TraceyFile::debug_print() const {
    std::cout << "File Debug Info:\n";
    std::cout << "  Path: " << (path_.empty() ? "(none)" : path_) << '\n';
    std::cout << "  Size: " << buffer_.size() << " bytes\n";
    if (!buffer_.empty()) {
        std::cout << "  First bytes (hex): ";
        for (std::size_t i = 0; i < std::min(buffer_.size(), std::size_t(16)); ++i) {
            std::cout << std::hex << (static_cast<unsigned int>(static_cast<unsigned char>(buffer_[i]))) << ' ';
        }
        std::cout << std::dec << '\n';
    }
}
