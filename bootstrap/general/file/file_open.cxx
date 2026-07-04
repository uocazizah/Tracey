#include <cstdio>
#include <cstdlib>
#include <tracey/file/file.hxx>

int tracey_file_open(TraceyFile *file, const char *path)
{
    if (file == NULL || path == NULL)
        return -1; // Parameter tidak valid

    FILE *f = fopen(path, "rb");
    if (f == NULL)
        return -1; // Gagal membuka file

    // Dapatkan ukuran file
    if (fseek(f, 0, SEEK_END) != 0) {
        fclose(f);
        return -1;
    }
    long size = ftell(f);
    if (size < 0) {
        fclose(f);
        return -1;
    }
    rewind(f);

    // Alokasi buffer
    char *buf = (char*)malloc(size);
    if (buf == NULL) {
        fclose(f);
        return -1;
    }

    // Baca seluruh file ke buffer
    size_t bytes_read = fread(buf, 1, size, f);
    if (bytes_read != (size_t)size) {
        free(buf);
        fclose(f);
        return -1;
    }

    // Isi struct
    file->path   = path;
    file->handle = f;
    file->buffer = buf;
    file->size   = (size_t)size;

    return 0; // Sukses
}
