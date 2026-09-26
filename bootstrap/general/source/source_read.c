#define _POSIX_C_SOURCE 200809L

#include <tracey/source.h>
#include "source_internal.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

tracey_source_t* tracey_source_read(const char* path)
{
    FILE* fp = NULL;
    struct stat st;
    size_t size = 0;
    char* content = NULL;
    size_t read = 0;
    tracey_source_t* src = NULL;

    if (!path || path[0] == '\0') return NULL;

    fp = fopen(path, "rb");
    if (!fp) return NULL;

    if (stat(path, &st) != 0) {
        fclose(fp);
        return NULL;
    }

    size = (size_t)st.st_size;
    content = malloc(size + 1);
    if (!content) {
        fclose(fp);
        return NULL;
    }

    read = fread(content, 1, size, fp);
    fclose(fp);

    if (read != size) {
        free(content);
        return NULL;
    }

    content[size] = '\0';

    src = malloc(sizeof(tracey_source_t));
    if (!src) {
        free(content);
        return NULL;
    }

    src->path = strdup(path);
    if (!src->path) {
        free(content);
        free(src);
        return NULL;
    }

    src->content = content;
    src->size = size;

    return src;
}

void tracey_source_free(tracey_source_t* src)
{
    if (!src) return;
    free(src->path);
    free(src->content);
    free(src);
}