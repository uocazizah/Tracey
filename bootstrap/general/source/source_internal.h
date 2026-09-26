#ifndef TRACEY_SOURCE_INTERNAL_H
#define TRACEY_SOURCE_INTERNAL_H

#include <tracey/source.h>
#include <stddef.h>

struct tracey_source {
    char* path;
    char* content;
    size_t size;
};

#endif /* TRACEY_SOURCE_INTERNAL_H */