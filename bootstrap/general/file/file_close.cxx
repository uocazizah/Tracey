#include <cstdio>
#include <cstdlib>
#include <tracey/file/file.hxx>

void tracey_file_close(TraceyFile *file)
{
    if (file == NULL)
        return;

    if (file->buffer != NULL) {
        free(file->buffer);
        file->buffer = NULL;
    }

    if (file->handle != NULL) {
        fclose(file->handle);
        file->handle = NULL;
    }

    file->path = NULL;
    file->size = 0;
}
