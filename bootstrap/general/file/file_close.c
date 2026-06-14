#include <stdio.h>
#include <stdlib.h>
#include <tracey/file/file.h>

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
