#include <tracey/common.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void tracey_string_init(struct tracey_string *s)
{
    if (!s)
        return;
    s->data = NULL;
    s->len = 0;
    s->cap = 0;
}

void tracey_string_free(struct tracey_string *s)
{
    if (!s)
        return;
    free(s->data);
    s->data = NULL;
    s->len = 0;
    s->cap = 0;
}

static bool tracey_string_grow(struct tracey_string *s, size_t needed)
{
    size_t new_cap;
    char *new_data;

    if (s->cap >= needed)
        return true;

    new_cap = s->cap ? s->cap * 2 : 32;
    while (new_cap < needed)
        new_cap *= 2;

    new_data = (char *)realloc(s->data, new_cap);
    if (!new_data)
        return false;

    s->data = new_data;
    s->cap = new_cap;
    return true;
}

bool tracey_string_append(struct tracey_string *s, const char *data, size_t len)
{
    if (!s || (!data && len > 0))
        return false;
    if (len == 0)
        return true;
    if (!tracey_string_grow(s, s->len + len + 1))
        return false;
    memcpy(s->data + s->len, data, len);
    s->len += len;
    s->data[s->len] = '\0';
    return true;
}

bool tracey_string_append_cstr(struct tracey_string *s, const char *cstr)
{
    if (!cstr)
        return true;
    return tracey_string_append(s, cstr, strlen(cstr));
}

bool tracey_string_append_char(struct tracey_string *s, char c)
{
    return tracey_string_append(s, &c, 1);
}

struct tracey_string_view tracey_string_view(const struct tracey_string *s)
{
    struct tracey_string_view sv;

    if (!s || !s->data)
    {
        sv.data = NULL;
        sv.len = 0;
        return sv;
    }
    sv.data = s->data;
    sv.len = s->len;
    return sv;
}

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
