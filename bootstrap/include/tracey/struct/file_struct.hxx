#pragma once

typedef struct TraceyFile
{
    const char *path;
    FILE *handle;

    char *buffer;
    size_t size;
} TraceyFile;
