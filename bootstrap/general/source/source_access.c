#include <tracey/source.h>
#include "source_internal.h"

const char* tracey_source_path(const tracey_source_t* src)
{
    return src ? src->path : NULL;
}

const char* tracey_source_content(const tracey_source_t* src)
{
    return src ? src->content : NULL;
}

size_t tracey_source_size(const tracey_source_t* src)
{
    return src ? src->size : 0;
}

bool tracey_source_is_empty(const tracey_source_t* src)
{
    return src ? src->size == 0 : true;
}