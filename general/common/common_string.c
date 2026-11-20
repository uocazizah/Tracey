#include "tracey/common.h"

#include <stdlib.h>
#include <string.h>

/*
 * tracey_string_grow - ensure the string buffer can hold at least needed bytes.
 *
 * s points to the string whose backing storage may be reallocated. The
 * function is an internal helper shared only within this compilation unit.
 * It does not take ownership of any external buffer and leaves s unchanged
 * on allocation failure.
 *
 * Returns true when the capacity is sufficient (possibly after growth), or
 * false when reallocation fails.
 */
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