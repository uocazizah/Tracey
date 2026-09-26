#ifndef TRACEY_SOURCE_H
#define TRACEY_SOURCE_H

#include <stddef.h>
#include <stdbool.h>
#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct tracey_source tracey_source_t;

tracey_source_t* tracey_source_read(const char* path);
void tracey_source_free(tracey_source_t* src);

const char* tracey_source_path(const tracey_source_t* src);
const char* tracey_source_content(const tracey_source_t* src);
size_t tracey_source_size(const tracey_source_t* src);
bool tracey_source_is_empty(const tracey_source_t* src);

void tracey_source_print_debug(const tracey_source_t* src, FILE* out);

#ifdef __cplusplus
}
#endif

#endif /* TRACEY_SOURCE_H */