#include <tracey/file/file.hxx>
#include <cstdio>
#include <cstdint>
#include <cerrno>
#include <system_error>

bool tracey::file::TraceyFile::open(const char* path) {
    // Jika sudah terbuka, tolak
    if (handle_) {
        return false;
    }

    // Buka file binary read-only
    std::FILE* f = std::fopen(path, "rb");
    if (!f) {
        return false;
    }

    // Dapatkan ukuran file
    if (std::fseek(f, 0, SEEK_END) != 0) {
        std::fclose(f);
        return false;
    }
    long sz = std::ftell(f);
    if (sz < 0) {
        std::fclose(f);
        return false;
    }
    std::rewind(f);

    // Alokasikan buffer dan baca
    std::vector<char> temp(static_cast<std::size_t>(sz));
    if (!temp.empty()) {
        std::size_t read = std::fread(temp.data(), 1, temp.size(), f);
        if (read != temp.size()) {
            // Gagal membaca sempurna; bersihkan dan kembalikan false
            std::fclose(f);
            return false;
        }
    }

    // Simpan state
    handle_ = f;
    path_ = path;
    buffer_ = std::move(temp);
    return true;
}
