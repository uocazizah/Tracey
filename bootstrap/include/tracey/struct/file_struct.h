#ifndef FILE_STRUCT_H
#define FILE_STRUCT_H

typedef struct TraceyFile
{
    const char *path;
    FILE *handle;

    char *buffer;
    size_t size;
} TraceyFile;

#endif /* FILE_STRUCT_H */
