#ifndef TRACEY_COMMON_H
#define TRACEY_COMMON_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * tracey_string_view - non-owning view into a character buffer.
 *
 * data points to the first character of the view. The view does not own the
 * memory and is only valid as long as the underlying buffer remains alive.
 * len is the number of bytes in the view; the view is not required to be
 * NUL-terminated.
 */
struct tracey_string_view
{
    const char *data;
    size_t len;
};

static inline struct tracey_string_view tracey_sv(const char *data, size_t len)
{
    struct tracey_string_view sv;
    sv.data = data;
    sv.len = len;
    return sv;
}

static inline struct tracey_string_view tracey_sv_from_cstr(const char *s)
{
    struct tracey_string_view sv;
    sv.data = s;
    sv.len = s ? strlen(s) : 0;
    return sv;
}

static inline bool tracey_sv_eq(struct tracey_string_view a, struct tracey_string_view b)
{
    if (a.len != b.len)
        return false;
    if (a.len == 0)
        return true;
    return memcmp(a.data, b.data, a.len) == 0;
}

static inline bool tracey_sv_eq_cstr(struct tracey_string_view a, const char *s)
{
    size_t slen = s ? strlen(s) : 0;
    if (a.len != slen)
        return false;
    if (slen == 0)
        return true;
    return memcmp(a.data, s, slen) == 0;
}

/*
 * tracey_string - owned, growable NUL-terminated string buffer.
 *
 * data points to a heap-allocated buffer of at least cap bytes, or NULL when
 * cap is zero. len is the number of bytes before the terminating NUL.
 * cap is the total allocated size including space for the terminator.
 */
struct tracey_string
{
    char *data;
    size_t len;
    size_t cap;
};

void tracey_string_init(struct tracey_string *s);
void tracey_string_free(struct tracey_string *s);
bool tracey_string_append(struct tracey_string *s, const char *data, size_t len);
bool tracey_string_append_cstr(struct tracey_string *s, const char *cstr);
bool tracey_string_append_char(struct tracey_string *s, char c);
struct tracey_string_view tracey_string_view(const struct tracey_string *s);

/*
 * tracey_buf - generic growable byte buffer.
 *
 * Used as a building block for dynamic arrays of bytes and as backing storage
 * for string tables and other variable-length data.
 */
struct tracey_buf
{
    uint8_t *data;
    size_t len;
    size_t cap;
};

void tracey_buf_init(struct tracey_buf *b);
void tracey_buf_free(struct tracey_buf *b);
bool tracey_buf_append(struct tracey_buf *b, const void *data, size_t len);
bool tracey_buf_append_byte(struct tracey_buf *b, uint8_t v);
bool tracey_buf_reserve(struct tracey_buf *b, size_t extra);

/*
 * xmalloc / xrealloc / xcalloc - allocation helpers that abort on failure.
 *
 * These wrappers simplify call sites in the compiler where out-of-memory is
 * treated as a fatal condition. Callers that need to handle allocation
 * failure gracefully should use the standard allocation functions directly.
 */
void *tracey_xmalloc(size_t size);
void *tracey_xrealloc(void *ptr, size_t size);
void *tracey_xcalloc(size_t nmemb, size_t size);

#ifdef __cplusplus
}
#endif

#endif /* TRACEY_COMMON_H */
