#ifndef FILE_H
#define FILE_H

#include <tracey/struct/file_struct.h>

int tracey_file_open(TraceyFile *file, const char *path);
void tracey_file_close(TraceyFile *file);

#endif /* FILE_H */
