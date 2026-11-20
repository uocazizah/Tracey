#include "tracey/common.h"

#include <stdio.h>
#include <stdlib.h>

void *tracey_xmalloc(size_t size)
{
    void *p = malloc(size);
    if (!p && size != 0)
    {
        fprintf(stderr, "tracey: out of memory\n");
        abort();
    }
    return p;
}

void *tracey_xrealloc(void *ptr, size_t size)
{
    void *p = realloc(ptr, size);
    if (!p && size != 0)
    {
        fprintf(stderr, "tracey: out of memory\n");
        abort();
    }
    return p;
}

void *tracey_xcalloc(size_t nmemb, size_t size)
{
    void *p = calloc(nmemb, size);
    if (!p && nmemb != 0 && size != 0)
    {
        fprintf(stderr, "tracey: out of memory\n");
        abort();
    }
    return p;
}