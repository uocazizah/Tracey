#include "tracey/common.h"

#include <stdlib.h>
#include <string.h>

void tracey_buf_init(struct tracey_buf *b)
{
    if (!b)
        return;
    b->data = NULL;
    b->len = 0;
    b->cap = 0;
}

void tracey_buf_free(struct tracey_buf *b)
{
    if (!b)
        return;
    free(b->data);
    b->data = NULL;
    b->len = 0;
    b->cap = 0;
}

bool tracey_buf_reserve(struct tracey_buf *b, size_t extra)
{
    size_t needed;
    size_t new_cap;
    uint8_t *new_data;

    if (!b)
        return false;
    if (extra == 0)
        return true;
    if (b->cap - b->len >= extra)
        return true;

    needed = b->len + extra;
    new_cap = b->cap ? b->cap * 2 : 32;
    while (new_cap < needed)
        new_cap *= 2;

    new_data = (uint8_t *)realloc(b->data, new_cap);
    if (!new_data)
        return false;

    b->data = new_data;
    b->cap = new_cap;
    return true;
}

bool tracey_buf_append(struct tracey_buf *b, const void *data, size_t len)
{
    if (!b || (!data && len > 0))
        return false;
    if (len == 0)
        return true;
    if (!tracey_buf_reserve(b, len))
        return false;
    memcpy(b->data + b->len, data, len);
    b->len += len;
    return true;
}

bool tracey_buf_append_byte(struct tracey_buf *b, uint8_t v)
{
    return tracey_buf_append(b, &v, 1);
}