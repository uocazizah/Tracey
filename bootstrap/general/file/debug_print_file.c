#include <stdio.h>
#include <tracey/file/file.h>

void tracey_debug_file_print(TraceyFile *file)
{
    if (file == NULL) {
        fprintf(stdout, "[TraceyFile] NULL pointer.\n");
        return;
    }

    if (file->buffer == NULL || file->size == 0) {
        fprintf(stdout, "[TraceyFile] No buffer or empty file (path: %s)\n",
                file->path ? file->path : "unknown");
        return;
    }

    fprintf(stdout, "--- TraceyFile: %s (size = %zu bytes) ---\n",
            file->path ? file->path : "unknown", file->size);

    // Cetak isi buffer per karakter (aman untuk biner maupun teks)
    for (size_t i = 0; i < file->size; i++) {
        fprintf(stdout, "%c", file->buffer[i]);
    }

    fprintf(stdout, "\n--- End of TraceyFile ---\n");
}
