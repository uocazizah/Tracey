/*
 * AArch64 assembler.
 *
 * This module belongs to the Tracey bootstrap toolchain and translates the
 * restricted assembly dialect produced by the code generator into machine
 * code, DWARF4 line information and ELF64 relocation metadata.
 *
 * The accepted dialect is deliberately small:
 *
 *   - one directive or instruction per line;
 *   - a line whose last non-blank character is ':' defines a label;
 *   - .file, .loc, .globl, .text, .data and .asciz are recognized, every
 *     other directive is ignored.
 *
 * There is no per-section bookkeeping: .text, .data, .rodata and .bss all
 * contribute to one flat output blob. Label offsets, relocation offsets and
 * symbol values are therefore byte offsets into that single blob, exactly as
 * in the assembler this implementation replaces.
 *
 * Encoding errors are recoverable in the sense that the assembler returns a
 * result structure with ok == false and a diagnostic instead of aborting.
 * The assembler does not attempt to resynchronize or continue after an
 * encoding error.
 */

#include "tracey/assembler.h"

#include "tracey/elf.h"

#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Value used in place of std::string::npos. */
#define ASM_NPOS ((size_t)-1)

/*
 * Base virtual address assumed for the .text blob when DWARF addresses are
 * generated. The assembler produces relocatable objects, so this address is
 * only used to fill in .debug_line and .debug_info; the linker relocates the
 * final program independently.
 */
#define ASM_BASE_ADDR ((uint64_t)0x401000)

/*
 * Maximum number of comma separated operands inspected by the encoder. The
 * widest supported instruction takes four operands; further operands are
 * counted but not stored, which keeps the arity checks below meaningful
 * without allocating per-instruction storage.
 */
#define ASM_MAX_OPERANDS 8

/*
 * R_AARCH64_CONDBR19 is not part of the shared ELF constants header, so the
 * number is kept local. R_AARCH64_JUMP26 and R_AARCH64_CALL26 come from
 * tracey/elf.h.
 */
#define ASM_RELOC_CONDBR19 280

/*
 * A non-owning view into a character buffer.
 *
 * p points at the first byte and n is the number of bytes. Views are never
 * NUL-terminated unless they happen to cover a complete line of the source
 * copy, so they must not be passed to string functions that require a
 * terminator.
 */
struct asm_slice
{
    const char *p;
    size_t n;
};

/*
 * Growable byte buffer used for the encoded text and for the DWARF sections.
 *
 * data owns the allocation (may be NULL while len is zero), cap is the
 * allocated size in bytes. failed latches an allocation failure so that
 * emission helpers can keep appending without checking every call; the
 * assembler tests the flag once before handing the buffer to the caller.
 */
struct asm_buf
{
    uint8_t *data;
    size_t len;
    size_t cap;
    bool failed;
};

/*
 * Generic growable array.
 *
 * items points to len elements of elem_size bytes each; cap is the number of
 * elements the allocation can hold. The vector does not know whether the
 * element type owns memory, so callers must release element contents before
 * calling asm_vec_free().
 */
struct asm_vec
{
    void *items;
    size_t len;
    size_t cap;
    size_t elem_size;
    bool failed;
};

/* Label recorded during the first pass. name is an owned C string. */
struct asm_label
{
    char *name;
    size_t offset;
};

/* Source position recorded for every instruction line. */
struct asm_line_entry
{
    size_t addr;
    int file;
    int line;
};

/* Exact encoding of a fully specified instruction form. */
struct asm_exact_form
{
    const char *mnemonic;
    const char *operands;
    uint32_t encoding;
};

/* Global label resolved against the label table (used for .debug_info). */
struct asm_func
{
    const char *name;
    size_t offset;
};

/* Outcome of encoding a single instruction line. */
enum asm_encode_status
{
    ASM_ENCODE_OK = 0,
    ASM_ENCODE_ERROR,
    ASM_ENCODE_NOMEM
};

/* State threaded through the per-instruction encoder. */
struct asm_emit
{
    struct asm_buf *p_text;
    struct asm_vec *p_relocs;
    const struct asm_vec *p_labels;
    struct tracey_asm_result *p_result;
    size_t off;
};

static struct asm_slice asm_make_slice(const char *p, size_t n)
{
    struct asm_slice s;

    s.p = p;
    s.n = n;
    return s;
}

static struct asm_slice asm_slice_cstr(const char *s)
{
    return asm_make_slice(s, s == NULL ? 0 : strlen(s));
}

/*
 * asm_slice_sub - return the sub-range starting at start.
 *
 * The behaviour mirrors std::string::substr(): a start beyond the end is
 * clamped instead of raising an error, and count is reduced to the number of
 * remaining bytes. ASM_NPOS may be passed as count to select the remainder.
 */
static struct asm_slice asm_slice_sub(struct asm_slice s, size_t start, size_t count)
{
    if (start > s.n)
        start = s.n;
    if (count > s.n - start)
        count = s.n - start;
    return asm_make_slice(s.p + start, count);
}

static struct asm_slice asm_slice_trim(struct asm_slice s)
{
    size_t first = 0;
    size_t last = s.n;

    while (first < last)
    {
        char c = s.p[first];
        if (c != ' ' && c != '\t' && c != '\r' && c != '\n')
            break;
        first++;
    }
    while (last > first)
    {
        char c = s.p[last - 1];
        if (c != ' ' && c != '\t' && c != '\r' && c != '\n')
            break;
        last--;
    }
    return asm_make_slice(s.p + first, last - first);
}

static bool asm_slice_eq(struct asm_slice s, const char *lit)
{
    size_t n = strlen(lit);

    if (s.n != n)
        return false;
    if (n == 0)
        return true;
    return memcmp(s.p, lit, n) == 0;
}

/* Compare a slice against a NUL-terminated string of unknown length. */
static bool asm_slice_eq_cstr(struct asm_slice s, const char *cstr)
{
    size_t n = strlen(cstr);

    if (s.n != n)
        return false;
    if (n == 0)
        return true;
    return memcmp(s.p, cstr, n) == 0;
}

static bool asm_slice_starts(struct asm_slice s, const char *prefix)
{
    size_t n = strlen(prefix);

    if (s.n < n)
        return false;
    if (n == 0)
        return true;
    return memcmp(s.p, prefix, n) == 0;
}

static size_t asm_slice_find_char(struct asm_slice s, char c)
{
    size_t i;

    for (i = 0; i < s.n; i++)
        if (s.p[i] == c)
            return i;
    return ASM_NPOS;
}

/*
 * asm_slice_find_from - find c at or after from.
 *
 * from is a byte position, or ASM_NPOS when a previous search failed; both
 * cases simply report "not found", matching std::string::find(c, npos).
 */
static size_t asm_slice_find_from(struct asm_slice s, char c, size_t from)
{
    size_t i;

    if (from >= s.n)
        return ASM_NPOS;
    for (i = from; i < s.n; i++)
        if (s.p[i] == c)
            return i;
    return ASM_NPOS;
}

static size_t asm_slice_rfind_char(struct asm_slice s, char c)
{
    size_t i = s.n;

    while (i > 0)
    {
        i--;
        if (s.p[i] == c)
            return i;
    }
    return ASM_NPOS;
}

/*
 * asm_slice_dup - allocate a NUL-terminated copy of s.
 *
 * Returns NULL when the allocation fails. The caller owns the returned
 * string and releases it with free().
 */
static char *asm_slice_dup(struct asm_slice s)
{
    char *copy = malloc(s.n + 1);

    if (copy == NULL)
        return NULL;
    if (s.n > 0)
        memcpy(copy, s.p, s.n);
    copy[s.n] = '\0';
    return copy;
}

/*
 * asm_after_char - return the remainder after the first occurrence of c.
 *
 * When c is absent the whole slice is returned, which mirrors the original
 * use of std::string::find() followed by substr(pos + 1): npos + 1 wraps to
 * zero in size_t arithmetic.
 */
static struct asm_slice asm_after_char(struct asm_slice s, char c)
{
    size_t pos = asm_slice_find_char(s, c);

    if (pos == ASM_NPOS)
        return s;
    return asm_slice_sub(s, pos + 1, ASM_NPOS);
}

/*
 * asm_quoted_payload - extract the text between the outermost double quotes.
 *
 * fallback is returned when the line does not contain two distinct quote
 * characters. The returned slice points into line.
 */
static struct asm_slice asm_quoted_payload(struct asm_slice line, struct asm_slice fallback)
{
    size_t q1 = asm_slice_find_char(line, '"');
    size_t q2 = asm_slice_rfind_char(line, '"');

    if (q1 == ASM_NPOS || q2 == q1)
        return fallback;
    return asm_slice_sub(line, q1 + 1, q2 - q1 - 1);
}

/*
 * asm_bracket_inside - return the text between '[' and ']'.
 *
 * lb and rb are the positions reported by asm_slice_find_char(); ASM_NPOS
 * means "not present". The clamping rules reproduce the original
 * substr(lb + 1, rb - lb - 1) expression, including the size_t wrap-around
 * that made a missing ']' select the rest of the line.
 */
static struct asm_slice asm_bracket_inside(struct asm_slice s, size_t lb, size_t rb)
{
    size_t start = (lb == ASM_NPOS) ? 0 : lb + 1;
    size_t end;

    if (start > s.n)
        start = s.n;
    if (rb == ASM_NPOS || rb < start || rb > s.n)
        end = s.n;
    else
        end = rb;
    return asm_make_slice(s.p + start, end - start);
}

/*
 * asm_parse_int - parse a decimal integer from the beginning of s.
 *
 * Leading white space and an optional sign are accepted; parsing stops at
 * the first character that is not a decimal digit. Returns false when no
 * digits are present. Values outside the int range saturate to INT_MIN or
 * INT_MAX, which replaces the std::stoi exception the original code would
 * have propagated to the caller.
 */
static bool asm_parse_int(struct asm_slice s, int *p_out)
{
    size_t i = 0;
    bool negative = false;
    unsigned long v = 0;

    while (i < s.n)
    {
        char c = s.p[i];
        if (c != ' ' && c != '\t' && c != '\n' && c != '\r' && c != '\f' && c != '\v')
            break;
        i++;
    }
    if (i < s.n && (s.p[i] == '+' || s.p[i] == '-'))
    {
        negative = (s.p[i] == '-');
        i++;
    }
    if (i >= s.n || s.p[i] < '0' || s.p[i] > '9')
        return false;
    while (i < s.n && s.p[i] >= '0' && s.p[i] <= '9')
    {
        if (v <= (ULONG_MAX - 9u) / 10u)
            v = v * 10u + (unsigned long)(s.p[i] - '0');
        else
            v = ULONG_MAX;
        i++;
    }
    if (negative)
    {
        if (v > (unsigned long)INT_MAX + 1u)
            v = (unsigned long)INT_MAX + 1u;
        *p_out = (int)(-(long)v);
    }
    else
    {
        if (v > (unsigned long)INT_MAX)
            v = (unsigned long)INT_MAX;
        *p_out = (int)v;
    }
    return true;
}

/*
 * asm_parse_filtered_int - parse an immediate after discarding other text.
 *
 * The original implementation kept only '-' and decimal digits from the
 * operand text and passed the result to std::stoi, so "#-8" and
 * "[x29, #-8]" both yield -8. The filtered text is bounded by a small stack
 * buffer: an immediate longer than the buffer is collapse to a saturated
 * value by asm_parse_int() anyway, and no code generator output comes close
 * to that length.
 */
static bool asm_parse_filtered_int(struct asm_slice s, int *p_out)
{
    char filtered[32];
    size_t i;
    size_t n = 0;

    for (i = 0; i < s.n; i++)
    {
        char c = s.p[i];
        if (c == '-' || isdigit((unsigned char)c))
        {
            if (n + 1 >= sizeof filtered)
                break;
            filtered[n++] = c;
        }
    }
    if (n == 0)
        return false;
    filtered[n] = '\0';
    return asm_parse_int(asm_make_slice(filtered, n), p_out);
}

/*
 * asm_regnum - decode a general purpose register operand.
 *
 * A trailing comma is removed to match the operand splitter of the original
 * assembler. "sp" and "xzr" map to 31, "lr" maps to 30 and xN/wN map to N.
 * Returns -1 when the operand does not denote a register or when the number
 * is not a valid decimal literal.
 */
static int asm_regnum(struct asm_slice r)
{
    int value = 0;

    if (r.n > 0 && r.p[r.n - 1] == ',')
        r.n--;
    if (asm_slice_eq(r, "sp") || asm_slice_eq(r, "xzr"))
        return 31;
    if (asm_slice_eq(r, "lr"))
        return 30;
    if (r.n > 0 && (r.p[0] == 'x' || r.p[0] == 'w'))
    {
        if (!asm_parse_int(asm_slice_sub(r, 1, ASM_NPOS), &value))
            return -1;
        return value;
    }
    return -1;
}

/*
 * asm_condcode - decode a condition code suffix.
 *
 * The comparison is case insensitive. Returns the AArch64 condition number
 * in the range 0..14, or -1 when the suffix is not a valid condition.
 */
static int asm_condcode(struct asm_slice c)
{
    char buf[16];
    size_t i;
    size_t n = c.n < sizeof buf - 1 ? c.n : sizeof buf - 1;

    for (i = 0; i < n; i++)
        buf[i] = (char)tolower((unsigned char)c.p[i]);
    buf[n] = '\0';

    if (strcmp(buf, "eq") == 0)
        return 0;
    if (strcmp(buf, "ne") == 0)
        return 1;
    if (strcmp(buf, "cs") == 0 || strcmp(buf, "hs") == 0)
        return 2;
    if (strcmp(buf, "cc") == 0 || strcmp(buf, "lo") == 0)
        return 3;
    if (strcmp(buf, "mi") == 0)
        return 4;
    if (strcmp(buf, "pl") == 0)
        return 5;
    if (strcmp(buf, "vs") == 0)
        return 6;
    if (strcmp(buf, "vc") == 0)
        return 7;
    if (strcmp(buf, "hi") == 0)
        return 8;
    if (strcmp(buf, "ls") == 0)
        return 9;
    if (strcmp(buf, "ge") == 0)
        return 10;
    if (strcmp(buf, "lt") == 0)
        return 11;
    if (strcmp(buf, "gt") == 0)
        return 12;
    if (strcmp(buf, "le") == 0)
        return 13;
    if (strcmp(buf, "al") == 0)
        return 14;
    return -1;
}

static void asm_buf_init(struct asm_buf *b)
{
    b->data = NULL;
    b->len = 0;
    b->cap = 0;
    b->failed = false;
}

static void asm_buf_free(struct asm_buf *b)
{
    free(b->data);
    b->data = NULL;
    b->len = 0;
    b->cap = 0;
}

/*
 * asm_buf_reserve - ensure room for extra more bytes.
 *
 * Returns false and latches b->failed when the allocation fails or when the
 * requested size would overflow. Subsequent emission helpers become no-ops
 * once the flag is set, so a caller may finish building a section and check
 * the flag once.
 */
static bool asm_buf_reserve(struct asm_buf *b, size_t extra)
{
    size_t need;
    size_t cap;
    uint8_t *p;

    if (b->failed)
        return false;
    if (extra > SIZE_MAX - b->len)
    {
        b->failed = true;
        return false;
    }
    need = b->len + extra;
    if (need <= b->cap)
        return true;
    cap = b->cap == 0 ? 64 : b->cap;
    while (cap < need)
    {
        if (cap > SIZE_MAX / 2)
        {
            cap = need;
            break;
        }
        cap *= 2;
    }
    p = realloc(b->data, cap);
    if (p == NULL)
    {
        b->failed = true;
        return false;
    }
    b->data = p;
    b->cap = cap;
    return true;
}

static void asm_buf_byte(struct asm_buf *b, uint8_t v)
{
    if (!asm_buf_reserve(b, 1))
        return;
    b->data[b->len++] = v;
}

static void asm_buf_bytes(struct asm_buf *b, const void *data, size_t len)
{
    if (len == 0)
        return;
    if (!asm_buf_reserve(b, len))
        return;
    memcpy(b->data + b->len, data, len);
    b->len += len;
}

static void asm_buf_u16(struct asm_buf *b, uint16_t v)
{
    if (!asm_buf_reserve(b, 2))
        return;
    b->data[b->len++] = (uint8_t)(v & 0xFFu);
    b->data[b->len++] = (uint8_t)((v >> 8) & 0xFFu);
}

static void asm_buf_u32(struct asm_buf *b, uint32_t v)
{
    if (!asm_buf_reserve(b, 4))
        return;
    b->data[b->len++] = (uint8_t)(v & 0xFFu);
    b->data[b->len++] = (uint8_t)((v >> 8) & 0xFFu);
    b->data[b->len++] = (uint8_t)((v >> 16) & 0xFFu);
    b->data[b->len++] = (uint8_t)((v >> 24) & 0xFFu);
}

static void asm_buf_u64(struct asm_buf *b, uint64_t v)
{
    int i;

    if (!asm_buf_reserve(b, 8))
        return;
    for (i = 0; i < 8; i++)
        b->data[b->len++] = (uint8_t)((v >> (8 * i)) & 0xFFu);
}

/* Append an unsigned LEB128 value, as required by the DWARF encodings. */
static void asm_buf_uleb(struct asm_buf *b, uint64_t v)
{
    do
    {
        uint8_t byte = (uint8_t)(v & 0x7Fu);

        v >>= 7;
        if (v != 0)
            byte |= 0x80u;
        asm_buf_byte(b, byte);
    } while (v != 0);
}

/*
 * Append a signed LEB128 value.
 *
 * The shift of a negative int64_t is an arithmetic shift on every supported
 * compiler, which is what the terminating condition below relies upon.
 */
static void asm_buf_sleb(struct asm_buf *b, int64_t v)
{
    bool more = true;

    while (more)
    {
        uint8_t byte = (uint8_t)(v & 0x7F);

        v >>= 7;
        if ((v == 0 && (byte & 0x40u) == 0) || (v == -1 && (byte & 0x40u) != 0))
            more = false;
        else
            byte |= 0x80u;
        asm_buf_byte(b, byte);
    }
}

/* Overwrite four already emitted bytes with a little-endian value. */
static void asm_buf_patch_u32(struct asm_buf *b, size_t pos, uint32_t v)
{
    size_t i;

    if (b->failed || pos > b->len || b->len - pos < 4)
        return;
    for (i = 0; i < 4; i++)
        b->data[pos + i] = (uint8_t)((v >> (8 * i)) & 0xFFu);
}

/*
 * asm_buf_detach - hand the buffer contents to the caller.
 *
 * The allocation is shrunk to the number of valid bytes when possible; a
 * failed shrink keeps the original block, which is still valid for free().
 * The buffer is reset, so it must not be freed again by asm_buf_free().
 */
static uint8_t *asm_buf_detach(struct asm_buf *b, size_t *p_len)
{
    uint8_t *data = b->data;

    if (data != NULL && b->cap > b->len)
    {
        uint8_t *shrunk = realloc(data, b->len);
        if (shrunk != NULL)
            data = shrunk;
    }
    *p_len = b->len;
    b->data = NULL;
    b->len = 0;
    b->cap = 0;
    return data;
}

static void asm_vec_init(struct asm_vec *v, size_t elem_size)
{
    v->items = NULL;
    v->len = 0;
    v->cap = 0;
    v->elem_size = elem_size;
    v->failed = false;
}

/*
 * asm_vec_push - append one zeroed element and return a pointer to it.
 *
 * Returns NULL when the allocation fails, in which case v->failed is set.
 * The returned pointer is invalidated by any later push to the same vector.
 */
static void *asm_vec_push(struct asm_vec *v)
{
    size_t cap;
    void *p;

    if (v->failed)
        return NULL;
    if (v->len == v->cap)
    {
        cap = v->cap == 0 ? 8 : v->cap;
        if (cap > SIZE_MAX / 2)
            cap = v->len + 1;
        else
            cap *= 2;
        if (cap < v->len + 1)
            cap = v->len + 1;
        if (cap > SIZE_MAX / v->elem_size)
        {
            v->failed = true;
            return NULL;
        }
        p = realloc(v->items, cap * v->elem_size);
        if (p == NULL)
        {
            v->failed = true;
            return NULL;
        }
        v->items = p;
        v->cap = cap;
    }
    p = (char *)v->items + v->len * v->elem_size;
    memset(p, 0, v->elem_size);
    v->len++;
    return p;
}

/*
 * asm_set_error - format a diagnostic into the result structure.
 *
 * The message is truncated when it does not fit into the fixed error buffer;
 * truncation never affects ok, which is always cleared here.
 */
static void asm_set_error(struct tracey_asm_result *r, const char *fmt, ...)
{
    va_list ap;

    va_start(ap, fmt);
    vsnprintf(r->error, sizeof r->error, fmt, ap);
    va_end(ap);
    r->ok = false;
}

/*
 * asm_result_release - free everything owned by a result, keeping the text
 * of the diagnostic intact.
 *
 * Used by tracey_asm_result_free() and by the assembler's error path.
 */
static void asm_result_release(struct tracey_asm_result *r)
{
    size_t i;

    free(r->text);
    r->text = NULL;
    r->text_len = 0;
    free(r->debug_line);
    r->debug_line = NULL;
    r->debug_line_len = 0;
    free(r->debug_info);
    r->debug_info = NULL;
    r->debug_info_len = 0;
    free(r->debug_abbrev);
    r->debug_abbrev = NULL;
    r->debug_abbrev_len = 0;
    for (i = 0; i < r->symbol_count; i++)
        free(r->symbols[i].name);
    free(r->symbols);
    r->symbols = NULL;
    r->symbol_count = 0;
    for (i = 0; i < r->reloc_count; i++)
        free(r->relocs[i].symbol);
    free(r->relocs);
    r->relocs = NULL;
    r->reloc_count = 0;
}

void tracey_asm_result_free(struct tracey_asm_result *r)
{
    if (r == NULL)
        return;
    asm_result_release(r);
    memset(r, 0, sizeof *r);
}

/*
 * asm_split_operands - split a comma separated operand list.
 *
 * Mirrors the loop used by the original encoder: a fragment is pushed for
 * every comma, even when empty, while the trailing fragment is pushed only
 * when it is not empty. Each fragment is trimmed. At most max_ops fragments
 * are stored; the returned count is clamped to max_ops as well.
 */
static size_t asm_split_operands(struct asm_slice operands, struct asm_slice *p_ops, size_t max_ops)
{
    size_t count = 0;
    size_t start = 0;
    size_t i;

    for (i = 0; i <= operands.n; i++)
    {
        if (i < operands.n && operands.p[i] != ',')
            continue;
        if (i < operands.n)
        {
            if (count < max_ops)
                p_ops[count] = asm_slice_trim(asm_make_slice(operands.p + start, i - start));
            count++;
            start = i + 1;
        }
        else if (start < operands.n)
        {
            if (count < max_ops)
                p_ops[count] =
                    asm_slice_trim(asm_make_slice(operands.p + start, operands.n - start));
            count++;
        }
    }
    if (count > max_ops)
        count = max_ops;
    return count;
}

/*
 * asm_compare_labels - qsort comparator ordering labels by name.
 *
 * Ordering labels by name reproduces the iteration order of the std::map the
 * original assembler used, which determines the order of .symtab entries.
 */
static int asm_compare_labels(const void *p_a, const void *p_b)
{
    const struct asm_label *const *a = p_a;
    const struct asm_label *const *b = p_b;

    return strcmp((*a)->name, (*b)->name);
}

static int asm_compare_funcs(const void *p_a, const void *p_b)
{
    const struct asm_func *a = p_a;
    const struct asm_func *b = p_b;

    if (a->offset < b->offset)
        return -1;
    if (a->offset > b->offset)
        return 1;
    return 0;
}

/*
 * asm_file_index - return the 1-based DWARF file index for name.
 *
 * The name is added to p_files when it is new. p_files owns duplicated
 * strings; a linear search keeps the insertion order and therefore the file
 * numbering identical to the original std::map based implementation.
 */
static bool asm_file_index(struct asm_vec *p_files, struct asm_slice name, int *p_index)
{
    char **items = p_files->items;
    char **slot;
    size_t i;

    for (i = 0; i < p_files->len; i++)
    {
        if (asm_slice_eq_cstr(name, items[i]))
        {
            *p_index = (int)(i + 1);
            return true;
        }
    }
    slot = asm_vec_push(p_files);
    if (slot == NULL)
        return false;
    *slot = asm_slice_dup(name);
    if (*slot == NULL)
        return false;
    *p_index = (int)p_files->len;
    return true;
}

/*
 * asm_label_set - define a label at the given byte offset.
 *
 * Redefining a label updates the existing entry, matching the behaviour of
 * std::map::operator[] in the original first pass.
 */
static bool asm_label_set(struct asm_vec *p_labels, struct asm_slice name, size_t offset)
{
    struct asm_label *items = p_labels->items;
    struct asm_label *slot;
    size_t i;

    for (i = 0; i < p_labels->len; i++)
    {
        if (asm_slice_eq_cstr(name, items[i].name))
        {
            items[i].offset = offset;
            return true;
        }
    }
    slot = asm_vec_push(p_labels);
    if (slot == NULL)
        return false;
    slot->name = asm_slice_dup(name);
    if (slot->name == NULL)
        return false;
    slot->offset = offset;
    return true;
}

/* Return the label entry for name, or NULL when it is not defined. */
static const struct asm_label *asm_label_find(const struct asm_vec *p_labels, struct asm_slice name)
{
    const struct asm_label *items = p_labels->items;
    size_t i;

    for (i = 0; i < p_labels->len; i++)
        if (asm_slice_eq_cstr(name, items[i].name))
            return &items[i];
    return NULL;
}

/*
 * asm_escaped_length - number of bytes produced by an .asciz payload.
 *
 * A backslash followed by any character counts as a single output byte; the
 * terminating NUL is not included.
 */
static size_t asm_escaped_length(struct asm_slice s)
{
    size_t i = 0;
    size_t len = 0;

    while (i < s.n)
    {
        if (s.p[i] == '\\' && i + 1 < s.n)
            i += 2;
        else
            i++;
        len++;
    }
    return len;
}
/*
 * asm_fail_msg - record a fixed diagnostic and report an encoding error.
 */
static enum asm_encode_status asm_fail_msg(struct asm_emit *e, const char *message)
{
    asm_set_error(e->p_result, "%s", message);
    return ASM_ENCODE_ERROR;
}

/*
 * asm_fail_operands - report an operand count or syntax error.
 *
 * This message is used for instructions that had no dedicated diagnostic in
 * the original assembler; the mnemonic is already lower-cased by the caller.
 */
static enum asm_encode_status asm_fail_operands(struct asm_emit *e, const char *mnemonic)
{
    asm_set_error(e->p_result, "Invalid operands for instruction: %s", mnemonic);
    return ASM_ENCODE_ERROR;
}

/* Append one instruction word at the current offset. */
static void asm_emit_word(struct asm_emit *e, uint32_t word)
{
    asm_buf_u32(e->p_text, word);
    e->off += 4;
}

/*
 * asm_emit_branch - emit a placeholder branch and its relocation.
 *
 * The relocation offset refers to the instruction that was just emitted, so
 * the offset is captured before the instruction advances the output
 * position. The symbol is duplicated because the source buffer is released
 * once assembly finishes.
 */
static enum asm_encode_status asm_emit_branch(struct asm_emit *e, uint32_t insn,
                                               struct asm_slice symbol, int type)
{
    struct tracey_asm_reloc *rel = asm_vec_push(e->p_relocs);

    if (rel == NULL)
        return ASM_ENCODE_NOMEM;
    rel->offset = e->off;
    rel->type = type;
    rel->symbol = asm_slice_dup(symbol);
    if (rel->symbol == NULL)
        return ASM_ENCODE_NOMEM;
    asm_buf_u32(e->p_text, insn);
    e->off += 4;
    return ASM_ENCODE_OK;
}

/*
 * Instruction forms that the code generator emits verbatim.
 *
 * Matching the exact operand text is equivalent to the original string key
 * comparison (mnemonic + " " + operands) because a mnemonic never contains a
 * space, and it avoids building a temporary key for every line.
 */
static const struct asm_exact_form asm_exact_forms[] = {
    {"stp", "x29, x30, [sp, #-16]!", 0xA9BF7BFDu},
    {"ldp", "x29, x30, [sp], #16", 0xA8C17BFDu},
    {"str", "x0, [sp, #-16]!", 0xF81F0FE0u},
    {"ldr", "x0, [sp], #16", 0xF84107E0u},
    {"str", "x0, [x29, #-8]", 0xF81F83A0u},
    {"ldr", "x0, [x29, #-8]", 0xF85F83A0u},
    {"mov", "x29, sp", 0x910003FDu},
    {"mov", "sp, x29", 0x910003BFu},
};

/*
 * asm_encode_line - encode one instruction line.
 *
 * mnemonic is the lower-cased instruction name and operands is the trimmed
 * remainder of the line. The emitters append to e->p_text and advance e->off
 * by four bytes per instruction.
 *
 * Returns ASM_ENCODE_OK when the instruction was encoded, ASM_ENCODE_ERROR
 * when the mnemonic or operands are invalid (the diagnostic is stored in
 * e->p_result) and ASM_ENCODE_NOMEM when an allocation failed.
 */
static enum asm_encode_status asm_encode_line(struct asm_emit *e, const char *mnemonic,
                                               struct asm_slice operands)
{
    struct asm_slice ops[ASM_MAX_OPERANDS];
    size_t op_count;
    size_t i;
    uint32_t insn;
    int rd;
    int rn;
    int rm;
    int ra;
    int cc;
    int imm;
    int shift;

    for (i = 0; i < sizeof asm_exact_forms / sizeof asm_exact_forms[0]; i++)
    {
        if (strcmp(mnemonic, asm_exact_forms[i].mnemonic) == 0 &&
            asm_slice_eq(operands, asm_exact_forms[i].operands))
        {
            asm_emit_word(e, asm_exact_forms[i].encoding);
            return ASM_ENCODE_OK;
        }
    }

    if (strcmp(mnemonic, "movk") == 0)
    {
        op_count = asm_split_operands(operands, ops, ASM_MAX_OPERANDS);
        if (op_count < 2)
            return asm_fail_operands(e, mnemonic);
        rd = asm_regnum(ops[0]);
        if (!asm_parse_int(asm_after_char(ops[1], '#'), &imm))
            return asm_fail_operands(e, mnemonic);
        shift = 0;
        if (op_count >= 3)
        {
            size_t hp = asm_slice_find_char(ops[2], '#');

            if (hp != ASM_NPOS &&
                !asm_parse_int(asm_slice_sub(ops[2], hp + 1, ASM_NPOS), &shift))
                return asm_fail_operands(e, mnemonic);
        }
        insn = 0xF2800000u | (((uint32_t)(shift / 16) & 0x3u) << 21) |
               (((uint32_t)imm & 0xFFFFu) << 5) | ((uint32_t)rd & 0x1Fu);
        asm_emit_word(e, insn);
        return ASM_ENCODE_OK;
    }

    if (strcmp(mnemonic, "fmov") == 0)
    {
        op_count = asm_split_operands(operands, ops, ASM_MAX_OPERANDS);
        if (op_count < 2)
            return asm_fail_msg(e, "Invalid floating-point move instruction");
        /* Destination in an FP register, source in a general register. */
        if (ops[0].n > 0 && ops[0].p[0] == 's' && ops[1].n > 0 && ops[1].p[0] == 'w')
        {
            if (!asm_parse_int(asm_slice_sub(ops[0], 1, ASM_NPOS), &rd) ||
                !asm_parse_int(asm_slice_sub(ops[1], 1, ASM_NPOS), &rn))
                return asm_fail_msg(e, "Invalid floating-point move instruction");
            insn = 0x1E270000u | (((uint32_t)rn & 0x1Fu) << 5) | ((uint32_t)rd & 0x1Fu);
            asm_emit_word(e, insn);
            return ASM_ENCODE_OK;
        }
        if (ops[0].n > 0 && ops[0].p[0] == 'd' && ops[1].n > 0 && ops[1].p[0] == 'x')
        {
            if (!asm_parse_int(asm_slice_sub(ops[0], 1, ASM_NPOS), &rd) ||
                !asm_parse_int(asm_slice_sub(ops[1], 1, ASM_NPOS), &rn))
                return asm_fail_msg(e, "Invalid floating-point move instruction");
            insn = 0x9E670000u | (((uint32_t)rn & 0x1Fu) << 5) | ((uint32_t)rd & 0x1Fu);
            asm_emit_word(e, insn);
            return ASM_ENCODE_OK;
        }
        /* Destination in a general register, source in an FP register. */
        if (ops[0].n > 0 && ops[0].p[0] == 'w' && ops[1].n > 0 && ops[1].p[0] == 's')
        {
            if (!asm_parse_int(asm_slice_sub(ops[0], 1, ASM_NPOS), &rd) ||
                !asm_parse_int(asm_slice_sub(ops[1], 1, ASM_NPOS), &rn))
                return asm_fail_msg(e, "Invalid floating-point move instruction");
            insn = 0x1E260000u | (((uint32_t)rn & 0x1Fu) << 5) | ((uint32_t)rd & 0x1Fu);
            asm_emit_word(e, insn);
            return ASM_ENCODE_OK;
        }
        if (ops[0].n > 0 && ops[0].p[0] == 'x' && ops[1].n > 0 && ops[1].p[0] == 'd')
        {
            if (!asm_parse_int(asm_slice_sub(ops[0], 1, ASM_NPOS), &rd) ||
                !asm_parse_int(asm_slice_sub(ops[1], 1, ASM_NPOS), &rn))
                return asm_fail_msg(e, "Invalid floating-point move instruction");
            insn = 0x9E660000u | (((uint32_t)rn & 0x1Fu) << 5) | ((uint32_t)rd & 0x1Fu);
            asm_emit_word(e, insn);
            return ASM_ENCODE_OK;
        }
        return asm_fail_msg(e, "Invalid floating-point move instruction");
    }

    if (strcmp(mnemonic, "fneg") == 0)
    {
        bool is_double;

        op_count = asm_split_operands(operands, ops, ASM_MAX_OPERANDS);
        if (op_count < 2)
            return asm_fail_operands(e, mnemonic);
        if (!asm_parse_int(asm_slice_sub(ops[0], 1, ASM_NPOS), &rd) ||
            !asm_parse_int(asm_slice_sub(ops[1], 1, ASM_NPOS), &rn))
            return asm_fail_operands(e, mnemonic);
        is_double = ops[0].n > 0 && ops[0].p[0] == 'd';
        insn = (is_double ? 0x1E614000u : 0x1E214000u) | (((uint32_t)rn & 0x1Fu) << 5) |
               ((uint32_t)rd & 0x1Fu);
        asm_emit_word(e, insn);
        return ASM_ENCODE_OK;
    }

    if (strcmp(mnemonic, "fcmp") == 0)
    {
        bool is_double;

        op_count = asm_split_operands(operands, ops, ASM_MAX_OPERANDS);
        if (op_count < 2)
            return asm_fail_operands(e, mnemonic);
        if (!asm_parse_int(asm_slice_sub(ops[0], 1, ASM_NPOS), &rn) ||
            !asm_parse_int(asm_slice_sub(ops[1], 1, ASM_NPOS), &rm))
            return asm_fail_operands(e, mnemonic);
        is_double = ops[0].n > 0 && ops[0].p[0] == 'd';
        insn = (is_double ? 0x1E602000u : 0x1E202000u) | (((uint32_t)rm & 0x1Fu) << 16) |
               (((uint32_t)rn & 0x1Fu) << 5);
        asm_emit_word(e, insn);
        return ASM_ENCODE_OK;
    }

    if (strcmp(mnemonic, "fadd") == 0 || strcmp(mnemonic, "fsub") == 0 ||
        strcmp(mnemonic, "fmul") == 0 || strcmp(mnemonic, "fdiv") == 0)
    {
        uint32_t opbase = 0;
        bool is_double;

        op_count = asm_split_operands(operands, ops, ASM_MAX_OPERANDS);
        if (op_count < 3)
            return asm_fail_operands(e, mnemonic);
        if (!asm_parse_int(asm_slice_sub(ops[0], 1, ASM_NPOS), &rd) ||
            !asm_parse_int(asm_slice_sub(ops[1], 1, ASM_NPOS), &rn) ||
            !asm_parse_int(asm_slice_sub(ops[2], 1, ASM_NPOS), &rm))
            return asm_fail_operands(e, mnemonic);
        is_double = ops[0].n > 0 && ops[0].p[0] == 'd';
        if (strcmp(mnemonic, "fadd") == 0)
            opbase = is_double ? 0x1E602800u : 0x1E202800u;
        else if (strcmp(mnemonic, "fsub") == 0)
            opbase = is_double ? 0x1E603800u : 0x1E203800u;
        else if (strcmp(mnemonic, "fmul") == 0)
            opbase = is_double ? 0x1E600800u : 0x1E200800u;
        else
            opbase = is_double ? 0x1E601800u : 0x1E201800u;
        insn = opbase | (((uint32_t)rm & 0x1Fu) << 16) | (((uint32_t)rn & 0x1Fu) << 5) |
               ((uint32_t)rd & 0x1Fu);
        asm_emit_word(e, insn);
        return ASM_ENCODE_OK;
    }

    if (strcmp(mnemonic, "and") == 0 || strcmp(mnemonic, "orr") == 0)
    {
        uint32_t base;

        op_count = asm_split_operands(operands, ops, ASM_MAX_OPERANDS);
        if (op_count < 3)
            return asm_fail_operands(e, mnemonic);
        if (!asm_parse_int(asm_slice_sub(ops[0], 1, ASM_NPOS), &rd) ||
            !asm_parse_int(asm_slice_sub(ops[1], 1, ASM_NPOS), &rn) ||
            !asm_parse_int(asm_slice_sub(ops[2], 1, ASM_NPOS), &rm))
            return asm_fail_operands(e, mnemonic);
        base = strcmp(mnemonic, "and") == 0 ? 0x8A000000u : 0xAA000000u;
        insn = base | (((uint32_t)rm & 0x1Fu) << 16) | (((uint32_t)rn & 0x1Fu) << 5) |
               ((uint32_t)rd & 0x1Fu);
        asm_emit_word(e, insn);
        return ASM_ENCODE_OK;
    }

    if (strcmp(mnemonic, "mov") == 0)
    {
        op_count = asm_split_operands(operands, ops, ASM_MAX_OPERANDS);
        if (op_count != 2)
            return asm_fail_msg(e, "Invalid move instruction");
        if (ops[1].n > 0 && ops[1].p[0] == '#')
        {
            /* MOVZ: sf=1, hw=0, imm16, Rd. A 64-bit move of a small value. */
            rd = asm_regnum(ops[0]);
            if (!asm_parse_int(asm_slice_sub(ops[1], 1, ASM_NPOS), &imm))
                return asm_fail_msg(e, "Invalid move instruction");
            insn = 0xD2800000u | ((uint32_t)rd & 0x1Fu) | (((uint32_t)imm & 0xFFFFu) << 5);
        }
        else
        {
            rd = asm_regnum(ops[0]);
            rn = asm_regnum(ops[1]);
            if (asm_slice_eq(ops[0], "sp") || asm_slice_eq(ops[1], "sp"))
            {
                /* MOV involving the stack pointer must be encoded as ADD. */
                insn = 0x91000000u | ((uint32_t)rd & 0x1Fu) | (((uint32_t)rn & 0x1Fu) << 5);
            }
            else
            {
                /* ORR Rd, XZR, Rm. */
                insn = 0xAA0003E0u | (((uint32_t)rn & 0x1Fu) << 16) | ((uint32_t)rd & 0x1Fu);
            }
        }
        asm_emit_word(e, insn);
        return ASM_ENCODE_OK;
    }

    if (strcmp(mnemonic, "add") == 0 || strcmp(mnemonic, "sub") == 0)
    {
        bool is_sub = strcmp(mnemonic, "sub") == 0;

        op_count = asm_split_operands(operands, ops, ASM_MAX_OPERANDS);
        if (op_count != 3)
            return asm_fail_msg(e, "Invalid add/subtract instruction");
        if (ops[2].n > 0 && ops[2].p[0] == '#')
        {
            rd = asm_regnum(ops[0]);
            rn = asm_regnum(ops[1]);
            if (!asm_parse_int(asm_slice_sub(ops[2], 1, ASM_NPOS), &imm))
                return asm_fail_msg(e, "Invalid add/subtract instruction");
            insn = (is_sub ? 0xD1000000u : 0x91000000u) | ((uint32_t)rd & 0x1Fu) |
                   (((uint32_t)rn & 0x1Fu) << 5) | (((uint32_t)imm & 0xFFFu) << 10);
        }
        else
        {
            rd = asm_regnum(ops[0]);
            rn = asm_regnum(ops[1]);
            rm = asm_regnum(ops[2]);
            insn = (is_sub ? 0xCB000000u : 0x8B000000u) | ((uint32_t)rd & 0x1Fu) |
                   (((uint32_t)rn & 0x1Fu) << 5) | (((uint32_t)rm & 0x1Fu) << 16);
        }
        asm_emit_word(e, insn);
        return ASM_ENCODE_OK;
    }

    if (strcmp(mnemonic, "udiv") == 0)
    {
        op_count = asm_split_operands(operands, ops, ASM_MAX_OPERANDS);
        if (op_count < 3)
            return asm_fail_operands(e, mnemonic);
        rd = asm_regnum(ops[0]);
        rn = asm_regnum(ops[1]);
        rm = asm_regnum(ops[2]);
        insn = 0x9AC00800u | ((uint32_t)rd & 0x1Fu) | (((uint32_t)rn & 0x1Fu) << 5) |
               (((uint32_t)rm & 0x1Fu) << 16);
        asm_emit_word(e, insn);
        return ASM_ENCODE_OK;
    }

    if (strcmp(mnemonic, "mul") == 0)
    {
        op_count = asm_split_operands(operands, ops, ASM_MAX_OPERANDS);
        if (op_count < 3)
            return asm_fail_operands(e, mnemonic);
        rd = asm_regnum(ops[0]);
        rn = asm_regnum(ops[1]);
        rm = asm_regnum(ops[2]);
        /* MUL 64-bit is MADD with Ra = XZR. */
        insn = 0x9B007C00u | ((uint32_t)rd & 0x1Fu) | (((uint32_t)rn & 0x1Fu) << 5) |
               (((uint32_t)rm & 0x1Fu) << 16);
        asm_emit_word(e, insn);
        return ASM_ENCODE_OK;
    }

    if (strcmp(mnemonic, "sdiv") == 0)
    {
        op_count = asm_split_operands(operands, ops, ASM_MAX_OPERANDS);
        if (op_count < 3)
            return asm_fail_operands(e, mnemonic);
        rd = asm_regnum(ops[0]);
        rn = asm_regnum(ops[1]);
        rm = asm_regnum(ops[2]);
        insn = 0x9AC00C00u | ((uint32_t)rd & 0x1Fu) | (((uint32_t)rn & 0x1Fu) << 5) |
               (((uint32_t)rm & 0x1Fu) << 16);
        asm_emit_word(e, insn);
        return ASM_ENCODE_OK;
    }

    if (strcmp(mnemonic, "msub") == 0)
    {
        op_count = asm_split_operands(operands, ops, ASM_MAX_OPERANDS);
        if (op_count < 4)
            return asm_fail_operands(e, mnemonic);
        rd = asm_regnum(ops[0]);
        rn = asm_regnum(ops[1]);
        rm = asm_regnum(ops[2]);
        ra = asm_regnum(ops[3]);
        insn = 0x9B008000u | ((uint32_t)rd & 0x1Fu) | (((uint32_t)rn & 0x1Fu) << 5) |
               (((uint32_t)ra & 0x1Fu) << 10) | (((uint32_t)rm & 0x1Fu) << 16);
        asm_emit_word(e, insn);
        return ASM_ENCODE_OK;
    }

    if (strcmp(mnemonic, "cmp") == 0)
    {
        op_count = asm_split_operands(operands, ops, ASM_MAX_OPERANDS);
        if (op_count < 2)
            return asm_fail_operands(e, mnemonic);
        if (op_count == 2 && ops[1].n > 0 && ops[1].p[0] == '#')
        {
            /* SUBS XZR, Xn, #imm. */
            rn = asm_regnum(ops[0]);
            if (!asm_parse_int(asm_slice_sub(ops[1], 1, ASM_NPOS), &imm))
                return asm_fail_operands(e, mnemonic);
            insn = 0xF100001Fu | (((uint32_t)rn & 0x1Fu) << 5) |
                   (((uint32_t)imm & 0xFFFu) << 10);
        }
        else
        {
            /* SUBS XZR, Xn, Xm. */
            rn = asm_regnum(ops[0]);
            rm = asm_regnum(ops[1]);
            insn = 0xEB00001Fu | (((uint32_t)rn & 0x1Fu) << 5) |
                   (((uint32_t)rm & 0x1Fu) << 16);
        }
        asm_emit_word(e, insn);
        return ASM_ENCODE_OK;
    }

    if (strcmp(mnemonic, "cset") == 0)
    {
        int inverted;

        op_count = asm_split_operands(operands, ops, ASM_MAX_OPERANDS);
        if (op_count < 2)
            return asm_fail_operands(e, mnemonic);
        rd = asm_regnum(ops[0]);
        cc = asm_condcode(ops[1]);
        /* CSINC Xd, XZR, XZR, invert(cond). */
        inverted = cc ^ 1;
        insn = 0x9A9F07E0u | ((uint32_t)rd & 0x1Fu) | ((uint32_t)inverted << 12);
        asm_emit_word(e, insn);
        return ASM_ENCODE_OK;
    }

    if (strcmp(mnemonic, "neg") == 0)
    {
        op_count = asm_split_operands(operands, ops, ASM_MAX_OPERANDS);
        if (op_count < 2)
            return asm_fail_operands(e, mnemonic);
        rd = asm_regnum(ops[0]);
        rn = asm_regnum(ops[1]);
        /* SUB Xd, XZR, Xm. */
        insn = 0xCB000000u | ((uint32_t)rd & 0x1Fu) | (31u << 5) |
               (((uint32_t)rn & 0x1Fu) << 16);
        asm_emit_word(e, insn);
        return ASM_ENCODE_OK;
    }

    if (strcmp(mnemonic, "ret") == 0)
    {
        asm_emit_word(e, 0xD65F03C0u);
        return ASM_ENCODE_OK;
    }

    if (strncmp(mnemonic, "b.", 2) == 0)
    {
        struct asm_slice target = asm_slice_trim(operands);

        cc = asm_condcode(asm_slice_cstr(mnemonic + 2));
        if (cc < 0)
            return asm_fail_msg(e, "Invalid branch condition");
        return asm_emit_branch(e, 0x54000000u | ((uint32_t)cc & 0xFu), target,
                               ASM_RELOC_CONDBR19);
    }

    if (strcmp(mnemonic, "cbz") == 0 || strcmp(mnemonic, "cbnz") == 0)
    {
        size_t comma = asm_slice_find_char(operands, ',');
        struct asm_slice rstr = asm_slice_trim(asm_slice_sub(operands, 0, comma));
        struct asm_slice target = asm_slice_trim(
            asm_slice_sub(operands, comma == ASM_NPOS ? 0 : comma + 1, ASM_NPOS));
        uint32_t base = strcmp(mnemonic, "cbnz") == 0 ? 0xB5000000u : 0xB4000000u;

        rn = asm_regnum(rstr);
        return asm_emit_branch(e, base | ((uint32_t)rn & 0x1Fu), target, ASM_RELOC_CONDBR19);
    }

    if (strcmp(mnemonic, "ldrb") == 0 || strcmp(mnemonic, "strb") == 0)
    {
        size_t lb = asm_slice_find_char(operands, '[');
        size_t rb = asm_slice_find_char(operands, ']');
        struct asm_slice rtstr = asm_slice_trim(asm_slice_sub(operands, 0, lb));
        struct asm_slice inside = asm_slice_trim(asm_bracket_inside(operands, lb, rb));
        int rt = asm_regnum(rtstr);
        int base = asm_regnum(inside);
        int offset = 0;
        size_t comma = asm_slice_find_char(inside, ',');

        if (comma != ASM_NPOS)
        {
            base = asm_regnum(asm_slice_sub(inside, 0, comma));
            /* An unparsable displacement leaves the offset at zero, which is
               what the original (conditionally parsed) code did. */
            if (!asm_parse_filtered_int(asm_slice_sub(inside, comma + 1, ASM_NPOS), &offset))
                offset = 0;
        }
        if (strcmp(mnemonic, "ldrb") == 0)
        {
            insn = 0x39400000u | (((uint32_t)offset & 0xFFFu) << 10) |
                   (((uint32_t)base & 0x1Fu) << 5) | ((uint32_t)rt & 0x1Fu);
        }
        else
        {
            uint32_t uimm = (uint32_t)offset & 0x1FFu;

            insn = 0x38000000u | (uimm << 12) | (((uint32_t)base & 0x1Fu) << 5) |
                   ((uint32_t)rt & 0x1Fu);
        }
        asm_emit_word(e, insn);
        return ASM_ENCODE_OK;
    }

    if (strcmp(mnemonic, "b") == 0 || strcmp(mnemonic, "beq") == 0 ||
        strcmp(mnemonic, "bl") == 0)
    {
        struct asm_slice target = asm_slice_trim(operands);
        uint32_t word;
        int type;

        if (strcmp(mnemonic, "bl") == 0)
        {
            word = 0x94000000u;
            type = (int)TRACEY_ELF_R_AARCH64_CALL26;
        }
        else if (strcmp(mnemonic, "b") == 0)
        {
            word = 0x14000000u;
            type = (int)TRACEY_ELF_R_AARCH64_JUMP26;
        }
        else
        {
            /* beq is the conditional form emitted by the code generator. */
            word = 0x54000000u;
            type = ASM_RELOC_CONDBR19;
        }
        return asm_emit_branch(e, word, target, type);
    }

    if (strcmp(mnemonic, "stp") == 0 || strcmp(mnemonic, "ldp") == 0)
    {
        size_t lb = asm_slice_find_char(operands, '[');
        size_t rb = asm_slice_find_char(operands, ']');
        struct asm_slice regs = asm_slice_trim(asm_slice_sub(operands, 0, lb));
        struct asm_slice ro[ASM_MAX_OPERANDS];
        struct asm_slice inside = asm_slice_trim(asm_bracket_inside(operands, lb, rb));
        size_t ro_count = asm_split_operands(regs, ro, ASM_MAX_OPERANDS);
        size_t first_lb = asm_slice_find_char(operands, '[');
        size_t first_rb = asm_slice_find_char(operands, ']');
        /* A post-index operand list has a comma after the closing bracket.
           The pre-index form is not detected: stp always encodes the
           pre-index base and ldp always encodes the post-index base, which is
           exactly what the previous implementation produced. */
        bool post = asm_slice_find_from(operands, ']', first_lb) != ASM_NPOS &&
                    asm_slice_find_from(operands, ',', first_rb) != ASM_NPOS;
        int rt;
        int rt2;
        int base = -1;
        int offset = 0;
        int imm7;
        uint32_t uimm;

        if (ro_count < 2)
            return asm_fail_operands(e, mnemonic);
        rt = asm_regnum(ro[0]);
        rt2 = asm_regnum(ro[1]);
        {
            size_t comma = asm_slice_find_char(inside, ',');

            if (comma == ASM_NPOS)
            {
                base = asm_regnum(inside);
            }
            else
            {
                struct asm_slice immediate =
                    asm_slice_trim(asm_slice_sub(inside, comma + 1, ASM_NPOS));

                base = asm_regnum(asm_slice_sub(inside, 0, comma));
                if (!asm_parse_int(asm_slice_sub(immediate, 1, ASM_NPOS), &offset))
                    return asm_fail_operands(e, mnemonic);
            }
        }
        if (post)
        {
            /* The displacement of the post-index form follows the closing
               bracket and is not part of the bracketed operand. */
            size_t rbp = asm_slice_find_from(operands, ']', rb);
            size_t after = (rbp == ASM_NPOS) ? 0 : rbp + 1;
            struct asm_slice rest = asm_slice_trim(asm_slice_sub(operands, after, ASM_NPOS));

            if (asm_slice_find_char(rest, '#') != ASM_NPOS &&
                !asm_parse_filtered_int(rest, &offset))
                offset = 0;
        }
        /* imm7 is a signed multiple of eight in the range -64..63. */
        imm7 = offset / 8;
        if (imm7 < -64)
            imm7 = -64;
        if (imm7 > 63)
            imm7 = 63;
        uimm = (uint32_t)imm7 & 0x7Fu;
        if (strcmp(mnemonic, "stp") == 0)
        {
            insn = 0xA9800000u | (uimm << 15) | (((uint32_t)rt2 & 0x1Fu) << 10) |
                   (((uint32_t)base & 0x1Fu) << 5) | ((uint32_t)rt & 0x1Fu);
        }
        else
        {
            insn = 0xA8C00000u | (uimm << 15) | (((uint32_t)rt2 & 0x1Fu) << 10) |
                   (((uint32_t)base & 0x1Fu) << 5) | ((uint32_t)rt & 0x1Fu);
        }
        asm_emit_word(e, insn);
        return ASM_ENCODE_OK;
    }

    if (strcmp(mnemonic, "ldr") == 0 || strcmp(mnemonic, "str") == 0)
    {
        bool is_store = strcmp(mnemonic, "str") == 0;
        size_t lb = asm_slice_find_char(operands, '[');
        size_t rb = asm_slice_find_char(operands, ']');
        struct asm_slice rtstr = asm_slice_trim(asm_slice_sub(operands, 0, lb));
        struct asm_slice inside = asm_slice_trim(asm_bracket_inside(operands, lb, rb));
        int rt = asm_regnum(rtstr);
        int base = -1;
        int offset = 0;
        int imm9;
        /* '!' after the closing bracket marks pre-index writeback, a comma
           after it marks post-index writeback. */
        bool pre = asm_slice_find_from(operands, '!', rb) != ASM_NPOS;
        bool post = asm_slice_find_from(operands, ',', rb) != ASM_NPOS;

        {
            size_t comma = asm_slice_find_char(inside, ',');

            if (comma == ASM_NPOS)
            {
                base = asm_regnum(inside);
            }
            else
            {
                base = asm_regnum(asm_slice_sub(inside, 0, comma));
                if (!asm_parse_filtered_int(asm_slice_sub(inside, comma + 1, ASM_NPOS),
                                            &offset))
                    return asm_fail_operands(e, mnemonic);
            }
        }
        {
            size_t rbp = asm_slice_find_from(operands, ']', rb);
            size_t after = (rbp == ASM_NPOS) ? 0 : rbp + 1;
            struct asm_slice rest = asm_slice_trim(asm_slice_sub(operands, after, ASM_NPOS));
            int post_offset;

            if (post && asm_parse_filtered_int(rest, &post_offset))
                offset = post_offset;
        }
        /* The 9-bit signed displacement is placed at bits 12..20. The
           original encoder used the same base for the pre-index and the
           plain signed-offset forms; that quirk is preserved here. */
        imm9 = offset & 0x1FF;
        if (pre)
        {
            insn = (is_store ? 0xF8000000u : 0xF8400000u) | ((uint32_t)imm9 << 12) |
                   ((uint32_t)base << 5) | (uint32_t)rt;
        }
        else if (post)
        {
            insn = (is_store ? 0xF8000C00u : 0xF8400C00u) | ((uint32_t)imm9 << 12) |
                   ((uint32_t)base << 5) | (uint32_t)rt;
        }
        else
        {
            insn = (is_store ? 0xF8000000u : 0xF8400000u) | ((uint32_t)imm9 << 12) |
                   ((uint32_t)base << 5) | (uint32_t)rt;
        }
        asm_emit_word(e, insn);
        return ASM_ENCODE_OK;
    }

    if (strcmp(mnemonic, "adr") == 0)
    {
        const struct asm_label *label;
        int64_t delta;

        op_count = asm_split_operands(operands, ops, ASM_MAX_OPERANDS);
        if (op_count < 2)
            return asm_fail_operands(e, mnemonic);
        rd = asm_regnum(ops[0]);
        {
            struct asm_slice target = asm_slice_trim(ops[1]);

            label = asm_label_find(e->p_labels, target);
            if (label == NULL)
            {
                asm_set_error(e->p_result, "Undefined assembler label: %.*s", (int)target.n,
                              target.p);
                return ASM_ENCODE_ERROR;
            }
            delta = (int64_t)label->offset - (int64_t)e->off;
        }
        /* ADR: immlo occupies bits 29..30, immhi bits 5..23. */
        insn = 0x10000000u | ((uint32_t)(delta & 0x3) << 29) |
               ((uint32_t)((delta >> 2) & 0x7FFFF) << 5) | ((uint32_t)rd & 0x1Fu);
        asm_emit_word(e, insn);
        return ASM_ENCODE_OK;
    }

    if (strcmp(mnemonic, "svc") == 0)
    {
        asm_emit_word(e, 0xD4000001u);
        return ASM_ENCODE_OK;
    }

    asm_set_error(e->p_result, "Unknown assembler instruction: %s", mnemonic);
    return ASM_ENCODE_ERROR;
}

/*
 * tracey_assemble_aarch64 - assemble one buffer of AArch64 source text.
 *
 * The input is copied so that lines can be terminated in place; the caller's
 * buffer is never modified or retained. Assembly runs in two passes: the
 * first records label offsets and the DWARF file list, the second encodes
 * instructions and fills in line table entries.
 *
 * On any error the result is left with ok == false and a diagnostic in
 * error; partially built output is released before returning.
 */
struct tracey_asm_result tracey_assemble_aarch64(const char *source, size_t source_len)
{
    struct tracey_asm_result res;
    char *src = NULL;
    struct asm_vec lines;
    struct asm_vec files;
    struct asm_vec labels;
    struct asm_vec globals;
    struct asm_vec entries;
    struct asm_vec symbols;
    struct asm_vec relocs;
    struct asm_buf text;
    struct asm_buf dbg;
    struct asm_buf abbrev;
    struct asm_buf info;
    struct asm_emit emit;
    struct asm_slice *line_items;
    struct asm_label **sorted_labels = NULL;
    size_t line_count;
    size_t unit_len_pos;
    size_t header_len_pos;
    size_t header_end;
    size_t ulen_pos;
    size_t off = 0;
    size_t i;
    size_t j;
    int cur_file = 1;
    int cur_line = 1;
    int file_index = 1;
    enum asm_encode_status status;

    memset(&res, 0, sizeof res);
    asm_vec_init(&lines, sizeof(struct asm_slice));
    asm_vec_init(&files, sizeof(char *));
    asm_vec_init(&labels, sizeof(struct asm_label));
    asm_vec_init(&globals, sizeof(char *));
    asm_vec_init(&entries, sizeof(struct asm_line_entry));
    asm_vec_init(&symbols, sizeof(struct tracey_asm_symbol));
    asm_vec_init(&relocs, sizeof(struct tracey_asm_reloc));
    asm_buf_init(&text);
    asm_buf_init(&dbg);
    asm_buf_init(&abbrev);
    asm_buf_init(&info);

    if (source == NULL)
        source_len = 0;

    /*
     * Copy the source and split it the way std::getline() did: a trailing
     * newline does not produce an extra empty line, and an empty buffer
     * produces no lines at all.
     */
    src = malloc(source_len + 1);
    if (src == NULL)
        goto oom;
    if (source_len > 0)
        memcpy(src, source, source_len);
    src[source_len] = '\0';
    {
        size_t start = 0;

        while (start < source_len)
        {
            size_t end = start;
            struct asm_slice *slot;

            while (end < source_len && src[end] != '\n')
                end++;
            slot = asm_vec_push(&lines);
            if (slot == NULL)
                goto oom;
            *slot = asm_make_slice(src + start, end - start);
            if (end >= source_len)
                break;
            start = end + 1;
        }
    }
    line_items = lines.items;
    line_count = lines.len;

    /* First pass: label offsets and the DWARF file table. */
    for (i = 0; i < line_count; i++)
    {
        struct asm_slice l = asm_slice_trim(line_items[i]);

        if (l.n == 0)
            continue;
        if (l.p[l.n - 1] == ':')
        {
            if (!asm_label_set(&labels, asm_slice_sub(l, 0, l.n - 1), off))
                goto oom;
            continue;
        }
        if (l.p[0] == '.')
        {
            if (asm_slice_starts(l, ".globl"))
                continue;
            if (asm_slice_eq(l, ".text"))
                continue;
            if (asm_slice_eq(l, ".data"))
                continue;
            if (asm_slice_starts(l, ".file"))
            {
                if (!asm_file_index(&files,
                                    asm_quoted_payload(l, asm_slice_cstr("unknown")),
                                    &file_index))
                    goto oom;
                continue;
            }
            if (asm_slice_starts(l, ".loc"))
                continue;
            if (asm_slice_starts(l, ".asciz"))
            {
                struct asm_slice payload = asm_quoted_payload(l, asm_slice_cstr(""));

                off += asm_escaped_length(payload) + 1;
                continue;
            }
            /* Unknown directives contribute no bytes. */
            continue;
        }
        off += 4;
    }

    if (files.len == 0)
    {
        if (!asm_file_index(&files, asm_slice_cstr("unknown"), &file_index))
            goto oom;
    }

    /* Second pass: encode instructions and record line table entries. */
    emit.p_text = &text;
    emit.p_relocs = &relocs;
    emit.p_labels = &labels;
    emit.p_result = &res;
    emit.off = 0;
    cur_file = 1;
    cur_line = 1;

    for (i = 0; i < line_count; i++)
    {
        struct asm_slice l = asm_slice_trim(line_items[i]);
        struct asm_slice mnemonic;
        struct asm_slice operands;
        char mnemonic_buf[64];
        size_t sp;
        size_t mn;
        size_t mi;

        if (l.n == 0)
            continue;
        if (l.p[l.n - 1] == ':')
            continue;
        if (l.p[0] == '.')
        {
            if (asm_slice_starts(l, ".file"))
            {
                if (!asm_file_index(&files,
                                    asm_quoted_payload(l, asm_slice_cstr("unknown")),
                                    &cur_file))
                    goto oom;
                continue;
            }
            if (asm_slice_starts(l, ".loc"))
            {
                int line_number;

                /* .loc <line> [<column>] [<options>]: only the line number
                   is consumed, as in the original stream extraction. */
                if (asm_parse_int(asm_slice_sub(l, 4, ASM_NPOS), &line_number))
                    cur_line = line_number;
                continue;
            }
            if (asm_slice_starts(l, ".globl"))
            {
                char **slot = asm_vec_push(&globals);

                if (slot == NULL)
                    goto oom;
                *slot = asm_slice_dup(asm_slice_trim(asm_slice_sub(l, 7, ASM_NPOS)));
                if (*slot == NULL)
                    goto oom;
            }
            else if (asm_slice_eq(l, ".text") || asm_slice_eq(l, ".data"))
            {
                /*
                 * Sections share one output blob, so switching sections
                 * carries no state. .rodata and .bss are not special-cased
                 * and fall into the unknown-directive path below.
                 */
            }
            else if (asm_slice_starts(l, ".asciz"))
            {
                struct asm_slice payload = asm_quoted_payload(l, asm_slice_cstr(""));
                size_t k = 0;
                size_t written = 0;

                while (k < payload.n)
                {
                    if (payload.p[k] == '\\' && k + 1 < payload.n)
                    {
                        char c = payload.p[k + 1];

                        if (c == 'n')
                        {
                            asm_buf_byte(&text, '\n');
                            k += 2;
                            written++;
                            continue;
                        }
                        if (c == 't')
                        {
                            asm_buf_byte(&text, '\t');
                            k += 2;
                            written++;
                            continue;
                        }
                        if (c == 'r')
                        {
                            asm_buf_byte(&text, '\r');
                            k += 2;
                            written++;
                            continue;
                        }
                        if (c == '0')
                        {
                            asm_buf_byte(&text, 0);
                            k += 2;
                            written++;
                            continue;
                        }
                        if (c == '\\')
                        {
                            asm_buf_byte(&text, '\\');
                            k += 2;
                            written++;
                            continue;
                        }
                        if (c == '"')
                        {
                            asm_buf_byte(&text, '"');
                            k += 2;
                            written++;
                            continue;
                        }
                        /*
                         * Unknown escape: the backslash is emitted and the
                         * following character is re-examined normally.
                         */
                        asm_buf_byte(&text, (uint8_t)payload.p[k]);
                    }
                    else
                    {
                        asm_buf_byte(&text, (uint8_t)payload.p[k]);
                    }
                    k++;
                    written++;
                }
                asm_buf_byte(&text, 0);
                written++;
                emit.off += written;
            }
            continue;
        }

        if (cur_line > 0)
        {
            struct asm_line_entry *entry = asm_vec_push(&entries);

            if (entry == NULL)
                goto oom;
            entry->addr = (size_t)(ASM_BASE_ADDR + (uint64_t)emit.off);
            entry->file = cur_file;
            entry->line = cur_line;
        }

        sp = ASM_NPOS;
        for (mn = 0; mn < l.n; mn++)
        {
            if (l.p[mn] == ' ' || l.p[mn] == '\t')
            {
                sp = mn;
                break;
            }
        }
        if (sp == ASM_NPOS)
        {
            mnemonic = l;
            operands = asm_slice_cstr("");
        }
        else
        {
            mnemonic = asm_slice_sub(l, 0, sp);
            operands = asm_slice_trim(asm_slice_sub(l, sp, ASM_NPOS));
        }
        mn = mnemonic.n < sizeof mnemonic_buf - 1 ? mnemonic.n : sizeof mnemonic_buf - 1;
        for (mi = 0; mi < mn; mi++)
            mnemonic_buf[mi] = (char)tolower((unsigned char)mnemonic.p[mi]);
        mnemonic_buf[mn] = '\0';

        status = asm_encode_line(&emit, mnemonic_buf, operands);
        if (status == ASM_ENCODE_NOMEM)
            goto oom;
        if (status == ASM_ENCODE_ERROR)
            goto fail;
    }

    /*
     * Build the symbol table from the label table. Labels are ordered by
     * name because the original stored them in a std::map, and that order
     * determines the .symtab layout.
     */
    if (labels.len > 0)
    {
        sorted_labels = malloc(labels.len * sizeof *sorted_labels);
        if (sorted_labels == NULL)
            goto oom;
        for (i = 0; i < labels.len; i++)
            sorted_labels[i] = (struct asm_label *)labels.items + i;
        qsort(sorted_labels, labels.len, sizeof *sorted_labels, asm_compare_labels);
        for (i = 0; i < labels.len; i++)
        {
            struct tracey_asm_symbol *sym = asm_vec_push(&symbols);

            if (sym == NULL)
                goto oom;
            /*
             * The symbol table owns its own copy. The label entry keeps its
             * name because the later DWARF function lookup still searches
             * the label table; clearing it here made that lookup strcmp()
             * a NULL pointer.
             */
            sym->name = asm_slice_dup(asm_slice_cstr(sorted_labels[i]->name));
            if (sym->name == NULL)
                goto oom;
            sym->offset = sorted_labels[i]->offset;
            sym->global = false;
        }
    }

    for (i = 0; i < globals.len; i++)
    {
        const char *name = ((char **)globals.items)[i];

        for (j = 0; j < symbols.len; j++)
        {
            struct tracey_asm_symbol *sym = (struct tracey_asm_symbol *)symbols.items + j;

            if (strcmp(sym->name, name) == 0)
                sym->global = true;
        }
    }

    /* DWARF4 .debug_line. */
    unit_len_pos = dbg.len;
    asm_buf_u32(&dbg, 0); /* unit_length, patched below */
    asm_buf_u16(&dbg, 4); /* version */
    header_len_pos = dbg.len;
    asm_buf_u32(&dbg, 0);                 /* header_length, patched below */
    asm_buf_byte(&dbg, 1);                /* minimum_instruction_length */
    asm_buf_byte(&dbg, 1);                /* maximum_operations_per_instruction */
    asm_buf_byte(&dbg, 1);                /* default_is_stmt */
    asm_buf_byte(&dbg, (uint8_t)-5);      /* line_base (signed -5) */
    asm_buf_byte(&dbg, 14);               /* line_range */
    asm_buf_byte(&dbg, 13);               /* opcode_base */
    {
        static const uint8_t std_opcode_lengths[12] = {0, 1, 1, 1, 1, 0, 0, 0, 1, 0, 0, 1};

        for (i = 0; i < 12; i++)
            asm_buf_byte(&dbg, std_opcode_lengths[i]);
    }
    asm_buf_byte(&dbg, 0); /* include_directories: empty list */
    for (i = 0; i < files.len; i++)
    {
        const char *name = ((char **)files.items)[i];

        asm_buf_bytes(&dbg, name, strlen(name));
        asm_buf_byte(&dbg, 0);
        asm_buf_uleb(&dbg, 0); /* directory index */
        asm_buf_uleb(&dbg, 0); /* modification time */
        asm_buf_uleb(&dbg, 0); /* file size */
    }
    asm_buf_byte(&dbg, 0); /* end of file list */
    header_end = dbg.len;
    asm_buf_patch_u32(&dbg, header_len_pos,
                      (uint32_t)(header_end - (header_len_pos + 4)));

    {
        size_t prev_addr = 0;
        int prev_line = 1;
        int prev_file = 1;
        struct asm_line_entry *items = entries.items;

        for (i = 0; i < entries.len; i++)
        {
            if (items[i].file != prev_file)
            {
                asm_buf_byte(&dbg, 4); /* DW_LNS_set_file */
                asm_buf_uleb(&dbg, (uint64_t)items[i].file);
                prev_file = items[i].file;
            }
            asm_buf_byte(&dbg, 3); /* DW_LNS_advance_line */
            asm_buf_sleb(&dbg, (int64_t)(items[i].line - prev_line));
            asm_buf_byte(&dbg, 2); /* DW_LNS_advance_pc */
            asm_buf_uleb(&dbg, (uint64_t)(items[i].addr - prev_addr));
            asm_buf_byte(&dbg, 1); /* DW_LNS_copy */
            prev_addr = items[i].addr;
            prev_line = items[i].line;
        }
    }
    asm_buf_byte(&dbg, 0);   /* DW_LNE_end_sequence */
    asm_buf_uleb(&dbg, 1);
    asm_buf_byte(&dbg, 1);
    asm_buf_patch_u32(&dbg, unit_len_pos, (uint32_t)(dbg.len - 4));

    /* .debug_abbrev: one compile unit and one subprogram declaration. */
    asm_buf_uleb(&abbrev, 1);
    asm_buf_byte(&abbrev, 0x11); /* DW_TAG_compile_unit */
    asm_buf_byte(&abbrev, 1);    /* DW_CHILDREN_yes */
    asm_buf_uleb(&abbrev, 0x03); /* DW_AT_name */
    asm_buf_uleb(&abbrev, 0x08); /* DW_FORM_string */
    asm_buf_uleb(&abbrev, 0x10); /* DW_AT_stmt_list */
    asm_buf_uleb(&abbrev, 0x17); /* DW_FORM_sec_offset */
    asm_buf_uleb(&abbrev, 0x11); /* DW_AT_low_pc */
    asm_buf_uleb(&abbrev, 0x01); /* DW_FORM_addr */
    asm_buf_uleb(&abbrev, 0x12); /* DW_AT_high_pc */
    asm_buf_uleb(&abbrev, 0x01); /* DW_FORM_addr */
    asm_buf_byte(&abbrev, 0);
    asm_buf_byte(&abbrev, 0);
    asm_buf_uleb(&abbrev, 2);
    asm_buf_byte(&abbrev, 0x2e); /* DW_TAG_subprogram */
    asm_buf_byte(&abbrev, 0);    /* DW_CHILDREN_no */
    asm_buf_uleb(&abbrev, 0x03); /* DW_AT_name */
    asm_buf_uleb(&abbrev, 0x08); /* DW_FORM_string */
    asm_buf_uleb(&abbrev, 0x11); /* DW_AT_low_pc */
    asm_buf_uleb(&abbrev, 0x01); /* DW_FORM_addr */
    asm_buf_uleb(&abbrev, 0x12); /* DW_AT_high_pc */
    asm_buf_uleb(&abbrev, 0x01); /* DW_FORM_addr */
    asm_buf_byte(&abbrev, 0);
    asm_buf_byte(&abbrev, 0);
    asm_buf_byte(&abbrev, 0); /* end of abbreviation table */

    /* .debug_info with one compile unit and one DIE per global label. */
    ulen_pos = info.len;
    asm_buf_u32(&info, 0); /* unit_length, patched below */
    asm_buf_u16(&info, 4); /* version */
    asm_buf_u32(&info, 0); /* debug_abbrev_offset */
    asm_buf_byte(&info, 8); /* address_size */
    asm_buf_uleb(&info, 1); /* abbreviation 1: compile unit */
    {
        const char *cu_name = files.len > 0 ? ((char **)files.items)[0] : "unknown";

        asm_buf_bytes(&info, cu_name, strlen(cu_name) + 1);
    }
    asm_buf_u32(&info, 0); /* DW_AT_stmt_list */
    asm_buf_u64(&info, ASM_BASE_ADDR);
    asm_buf_u64(&info, ASM_BASE_ADDR + (uint64_t)text.len);
    {
        struct asm_func *funcs = NULL;
        size_t func_count = 0;

        for (i = 0; i < globals.len; i++)
        {
            const char *name = ((char **)globals.items)[i];

            if (asm_label_find(&labels, asm_slice_cstr(name)) != NULL)
                func_count++;
        }
        if (func_count > 0)
        {
            funcs = malloc(func_count * sizeof *funcs);
            if (funcs == NULL)
                goto oom;
            func_count = 0;
            for (i = 0; i < globals.len; i++)
            {
                const char *name = ((char **)globals.items)[i];
                const struct asm_label *label = asm_label_find(&labels, asm_slice_cstr(name));

                if (label != NULL)
                {
                    funcs[func_count].name = name;
                    funcs[func_count].offset = label->offset;
                    func_count++;
                }
            }
            qsort(funcs, func_count, sizeof *funcs, asm_compare_funcs);
            for (i = 0; i < func_count; i++)
            {
                uint64_t high = (i + 1 < func_count)
                                    ? ASM_BASE_ADDR + (uint64_t)funcs[i + 1].offset
                                    : ASM_BASE_ADDR + (uint64_t)text.len;

                asm_buf_uleb(&info, 2); /* abbreviation 2: subprogram */
                asm_buf_bytes(&info, funcs[i].name, strlen(funcs[i].name) + 1);
                asm_buf_u64(&info, ASM_BASE_ADDR + (uint64_t)funcs[i].offset);
                asm_buf_u64(&info, high);
            }
            free(funcs);
        }
    }
    asm_buf_byte(&info, 0); /* end of children */
    asm_buf_patch_u32(&info, ulen_pos, (uint32_t)(info.len - 4));

    if (text.failed || dbg.failed || abbrev.failed || info.failed)
        goto oom;

    res.text = asm_buf_detach(&text, &res.text_len);
    res.debug_line = asm_buf_detach(&dbg, &res.debug_line_len);
    res.debug_info = asm_buf_detach(&info, &res.debug_info_len);
    res.debug_abbrev = asm_buf_detach(&abbrev, &res.debug_abbrev_len);
    res.symbols = symbols.items;
    res.symbol_count = symbols.len;
    symbols.items = NULL;
    symbols.len = 0;
    symbols.cap = 0;
    res.relocs = relocs.items;
    res.reloc_count = relocs.len;
    relocs.items = NULL;
    relocs.len = 0;
    relocs.cap = 0;
    res.ok = true;
    goto cleanup;

oom:
    asm_set_error(&res, "Out of memory while assembling AArch64 source");
fail:
    /* Encoding failed: drop whatever was built, keep the diagnostic. */
    asm_result_release(&res);
    res.ok = false;

cleanup:
    free(src);
    free(lines.items);
    for (i = 0; i < files.len; i++)
        free(((char **)files.items)[i]);
    free(files.items);
    for (i = 0; i < labels.len; i++)
        free(((struct asm_label *)labels.items)[i].name);
    free(labels.items);
    for (i = 0; i < globals.len; i++)
        free(((char **)globals.items)[i]);
    free(globals.items);
    free(entries.items);
    for (i = 0; i < symbols.len; i++)
        free(((struct tracey_asm_symbol *)symbols.items)[i].name);
    free(symbols.items);
    for (i = 0; i < relocs.len; i++)
        free(((struct tracey_asm_reloc *)relocs.items)[i].symbol);
    free(relocs.items);
    free(sorted_labels);
    asm_buf_free(&text);
    asm_buf_free(&dbg);
    asm_buf_free(&abbrev);
    asm_buf_free(&info);
    return res;
}

/*
 * asm_align8 - round a file offset up to an eight-byte boundary.
 *
 * The symbol table and relocation sections contain 64-bit fields, so their
 * file offsets must be naturally aligned for consumers that mmap the file.
 */
static size_t asm_align8(size_t value)
{
    return (value + 7u) & ~(size_t)7u;
}

/* Emit n zero bytes, used to pad between naturally aligned sections. */
static bool asm_write_pad(FILE *f, size_t n)
{
    static const unsigned char zeros[8] = {0};

    while (n > 0)
    {
        size_t chunk = n > sizeof zeros ? sizeof zeros : n;

        if (fwrite(zeros, 1, chunk, f) != chunk)
            return false;
        n -= chunk;
    }
    return true;
}

/* Append a NUL-terminated string to a string table and return its index. */
static size_t asm_strtab_add(struct asm_buf *b, const char *s)
{
    size_t index = b->len;

    asm_buf_bytes(b, s, strlen(s) + 1);
    return index;
}

/*
 * tracey_write_elf_object - serialize an assembly result as ELF64 REL.
 *
 * The object contains .text, .symtab, .strtab, .rela.text and .shstrtab plus
 * the three DWARF sections when they are non-empty. Relocations that refer to
 * an undefined label become references to an undefined global symbol, which
 * the linker resolves against the other input objects.
 *
 * The writer checks the stream error state once after all sections have been
 * emitted; a failure therefore reports false even if the underlying fwrite()
 * calls silently accepted a short write.
 */
bool tracey_write_elf_object(const char *path, const struct tracey_asm_result *r)
{
    struct asm_buf strtab;
    struct asm_buf shstrtab;
    struct tracey_elf_sym *syms = NULL;
    struct tracey_elf_rela *relas = NULL;
    struct tracey_elf_ehdr ehdr;
    struct tracey_elf_shdr shdr;
    size_t sym_count = 1;
    size_t rel_count = 0;
    size_t idx_text;
    size_t idx_symtab;
    size_t idx_strtab;
    size_t idx_rela;
    size_t idx_shstrtab;
    size_t idx_debug_line = 0;
    size_t idx_debug_info = 0;
    size_t idx_debug_abbrev = 0;
    size_t text_off;
    size_t symtab_off;
    size_t strtab_off;
    size_t rela_off;
    size_t shstr_off;
    size_t end_off;
    size_t debug_line_off = 0;
    size_t debug_info_off = 0;
    size_t debug_abbrev_off = 0;
    size_t shdr_off;
    size_t shnum;
    size_t i;
    FILE *f;
    bool ok = true;

    if (path == NULL || r == NULL)
        return false;

    f = fopen(path, "wb");
    if (f == NULL)
    {
        fprintf(stderr, "Error: cannot open output object '%s': %s\n", path, strerror(errno));
        return false;
    }

    asm_buf_init(&strtab);
    asm_buf_init(&shstrtab);
    asm_buf_byte(&strtab, 0);
    asm_buf_byte(&shstrtab, 0);

    idx_text = asm_strtab_add(&shstrtab, ".text");
    idx_symtab = asm_strtab_add(&shstrtab, ".symtab");
    idx_strtab = asm_strtab_add(&shstrtab, ".strtab");
    idx_rela = asm_strtab_add(&shstrtab, ".rela.text");
    idx_shstrtab = asm_strtab_add(&shstrtab, ".shstrtab");
    if (r->debug_line != NULL && r->debug_line_len > 0)
        idx_debug_line = asm_strtab_add(&shstrtab, ".debug_line");
    if (r->debug_info != NULL && r->debug_info_len > 0)
        idx_debug_info = asm_strtab_add(&shstrtab, ".debug_info");
    if (r->debug_abbrev != NULL && r->debug_abbrev_len > 0)
        idx_debug_abbrev = asm_strtab_add(&shstrtab, ".debug_abbrev");

    if (shstrtab.failed)
    {
        fprintf(stderr, "Error: out of memory while writing object\n");
        ok = false;
        goto cleanup;
    }

    /*
     * One null symbol followed by the module symbols. External relocation
     * targets discovered below append further undefined symbols.
     */
    if (r->symbol_count > SIZE_MAX - r->reloc_count - 1u)
    {
        fprintf(stderr, "Error: too many symbols while writing object\n");
        ok = false;
        goto cleanup;
    }
    syms = calloc(r->symbol_count + r->reloc_count + 1u, sizeof(*syms));
    relas = calloc(r->reloc_count > 0 ? r->reloc_count : 1u, sizeof(*relas));
    if (syms == NULL || relas == NULL)
    {
        fprintf(stderr, "Error: out of memory while writing object\n");
        ok = false;
        goto cleanup;
    }

    for (i = 0; i < r->symbol_count; i++)
    {
        struct tracey_elf_sym *s = &syms[sym_count];

        s->st_name = (uint32_t)asm_strtab_add(&strtab, r->symbols[i].name);
        s->st_info = (unsigned char)(((r->symbols[i].global ? TRACEY_ELF_STB_GLOBAL
                                                             : TRACEY_ELF_STB_LOCAL)
                                      << 4) |
                                     TRACEY_ELF_STT_FUNC);
        s->st_shndx = 1;
        s->st_value = (uint64_t)r->symbols[i].offset;
        s->st_size = 0;
        sym_count++;
    }

    for (i = 0; i < r->reloc_count; i++)
    {
        size_t si = 0;
        size_t j;

        for (j = 0; j < r->symbol_count; j++)
        {
            if (strcmp(r->symbols[j].name, r->relocs[i].symbol) == 0)
            {
                si = j + 1;
                break;
            }
        }
        if (si == 0)
        {
            struct tracey_elf_sym *s = &syms[sym_count];

            s->st_name = (uint32_t)asm_strtab_add(&strtab, r->relocs[i].symbol);
            s->st_info =
                (unsigned char)((TRACEY_ELF_STB_GLOBAL << 4) | TRACEY_ELF_STT_FUNC);
            s->st_shndx = TRACEY_ELF_SHN_UNDEF;
            s->st_value = 0;
            s->st_size = 0;
            si = sym_count;
            sym_count++;
        }
        relas[rel_count].r_offset = (uint64_t)r->relocs[i].offset;
        relas[rel_count].r_info =
            ((uint64_t)si << 32) | (uint64_t)(uint32_t)r->relocs[i].type;
        relas[rel_count].r_addend = 0;
        rel_count++;
    }

    if (strtab.failed)
    {
        fprintf(stderr, "Error: out of memory while writing object\n");
        ok = false;
        goto cleanup;
    }

    text_off = 64;
    symtab_off = asm_align8(text_off + r->text_len);
    strtab_off = symtab_off + sym_count * sizeof(struct tracey_elf_sym);
    rela_off = asm_align8(strtab_off + strtab.len);
    shstr_off = rela_off + rel_count * sizeof(struct tracey_elf_rela);
    end_off = shstr_off + shstrtab.len;
    if (idx_debug_line != 0)
    {
        debug_line_off = end_off;
        end_off += r->debug_line_len;
    }
    if (idx_debug_info != 0)
    {
        debug_info_off = end_off;
        end_off += r->debug_info_len;
    }
    if (idx_debug_abbrev != 0)
    {
        debug_abbrev_off = end_off;
        end_off += r->debug_abbrev_len;
    }
    shdr_off = asm_align8(end_off);
    shnum = 6u + (idx_debug_line != 0 ? 1u : 0u) + (idx_debug_info != 0 ? 1u : 0u) +
            (idx_debug_abbrev != 0 ? 1u : 0u);

    memset(&ehdr, 0, sizeof ehdr);
    ehdr.e_ident[0] = 0x7f;
    ehdr.e_ident[1] = 'E';
    ehdr.e_ident[2] = 'L';
    ehdr.e_ident[3] = 'F';
    ehdr.e_ident[4] = 2; /* ELFCLASS64 */
    ehdr.e_ident[5] = 1; /* ELFDATA2LSB */
    ehdr.e_ident[6] = 1; /* EV_CURRENT */
    ehdr.e_type = TRACEY_ELF_ET_REL;
    ehdr.e_machine = TRACEY_ELF_EM_AARCH64;
    ehdr.e_version = 1;
    ehdr.e_shoff = (uint64_t)shdr_off;
    ehdr.e_ehsize = 64;
    ehdr.e_shentsize = 64;
    ehdr.e_shnum = (uint16_t)shnum;
    ehdr.e_shstrndx = 5;

    tracey_elf_write_ehdr(f, &ehdr);

    if (fwrite(r->text, 1, r->text_len, f) != r->text_len)
        ok = false;
    if (ok && !asm_write_pad(f, symtab_off - (text_off + r->text_len)))
        ok = false;
    for (i = 0; ok && i < sym_count; i++)
        tracey_elf_write_sym(f, &syms[i]);
    if (ok && fwrite(strtab.data, 1, strtab.len, f) != strtab.len)
        ok = false;
    if (ok && !asm_write_pad(f, rela_off - (strtab_off + strtab.len)))
        ok = false;
    for (i = 0; ok && i < rel_count; i++)
        tracey_elf_write_rela(f, &relas[i]);
    if (ok && fwrite(shstrtab.data, 1, shstrtab.len, f) != shstrtab.len)
        ok = false;
    if (ok && idx_debug_line != 0)
    {
        if (fwrite(r->debug_line, 1, r->debug_line_len, f) != r->debug_line_len)
            ok = false;
    }
    if (ok && idx_debug_info != 0)
    {
        if (fwrite(r->debug_info, 1, r->debug_info_len, f) != r->debug_info_len)
            ok = false;
    }
    if (ok && idx_debug_abbrev != 0)
    {
        if (fwrite(r->debug_abbrev, 1, r->debug_abbrev_len, f) != r->debug_abbrev_len)
            ok = false;
    }
    if (ok && !asm_write_pad(f, shdr_off - end_off))
        ok = false;

    /* Section header table: null entry first. */
    memset(&shdr, 0, sizeof shdr);
    tracey_elf_write_shdr(f, &shdr);

    memset(&shdr, 0, sizeof shdr);
    shdr.sh_name = (uint32_t)idx_text;
    shdr.sh_type = TRACEY_ELF_SHT_PROGBITS;
    shdr.sh_flags = TRACEY_ELF_SHF_ALLOC | TRACEY_ELF_SHF_EXECINSTR;
    shdr.sh_offset = (uint64_t)text_off;
    shdr.sh_size = (uint64_t)r->text_len;
    shdr.sh_addralign = 4;
    tracey_elf_write_shdr(f, &shdr);

    memset(&shdr, 0, sizeof shdr);
    shdr.sh_name = (uint32_t)idx_symtab;
    shdr.sh_type = TRACEY_ELF_SHT_SYMTAB;
    shdr.sh_offset = (uint64_t)symtab_off;
    shdr.sh_size = (uint64_t)(sym_count * sizeof(struct tracey_elf_sym));
    shdr.sh_link = 3;
    shdr.sh_info = 1;
    shdr.sh_addralign = 8;
    shdr.sh_entsize = sizeof(struct tracey_elf_sym);
    tracey_elf_write_shdr(f, &shdr);

    memset(&shdr, 0, sizeof shdr);
    shdr.sh_name = (uint32_t)idx_strtab;
    shdr.sh_type = TRACEY_ELF_SHT_STRTAB;
    shdr.sh_offset = (uint64_t)strtab_off;
    shdr.sh_size = (uint64_t)strtab.len;
    shdr.sh_addralign = 1;
    tracey_elf_write_shdr(f, &shdr);

    memset(&shdr, 0, sizeof shdr);
    shdr.sh_name = (uint32_t)idx_rela;
    shdr.sh_type = TRACEY_ELF_SHT_RELA;
    shdr.sh_offset = (uint64_t)rela_off;
    shdr.sh_size = (uint64_t)(rel_count * sizeof(struct tracey_elf_rela));
    shdr.sh_link = 2;
    shdr.sh_info = 1;
    shdr.sh_addralign = 8;
    shdr.sh_entsize = sizeof(struct tracey_elf_rela);
    tracey_elf_write_shdr(f, &shdr);

    memset(&shdr, 0, sizeof shdr);
    shdr.sh_name = (uint32_t)idx_shstrtab;
    shdr.sh_type = TRACEY_ELF_SHT_STRTAB;
    shdr.sh_offset = (uint64_t)shstr_off;
    shdr.sh_size = (uint64_t)shstrtab.len;
    shdr.sh_addralign = 1;
    tracey_elf_write_shdr(f, &shdr);

    if (idx_debug_line != 0)
    {
        memset(&shdr, 0, sizeof shdr);
        shdr.sh_name = (uint32_t)idx_debug_line;
        shdr.sh_type = TRACEY_ELF_SHT_PROGBITS;
        shdr.sh_offset = (uint64_t)debug_line_off;
        shdr.sh_size = (uint64_t)r->debug_line_len;
        shdr.sh_addralign = 1;
        tracey_elf_write_shdr(f, &shdr);
    }
    if (idx_debug_info != 0)
    {
        memset(&shdr, 0, sizeof shdr);
        shdr.sh_name = (uint32_t)idx_debug_info;
        shdr.sh_type = TRACEY_ELF_SHT_PROGBITS;
        shdr.sh_offset = (uint64_t)debug_info_off;
        shdr.sh_size = (uint64_t)r->debug_info_len;
        shdr.sh_addralign = 1;
        tracey_elf_write_shdr(f, &shdr);
    }
    if (idx_debug_abbrev != 0)
    {
        memset(&shdr, 0, sizeof shdr);
        shdr.sh_name = (uint32_t)idx_debug_abbrev;
        shdr.sh_type = TRACEY_ELF_SHT_PROGBITS;
        shdr.sh_offset = (uint64_t)debug_abbrev_off;
        shdr.sh_size = (uint64_t)r->debug_abbrev_len;
        shdr.sh_addralign = 1;
        tracey_elf_write_shdr(f, &shdr);
    }

    if (ok && ferror(f))
        ok = false;

cleanup:
    if (fclose(f) != 0)
        ok = false;
    if (!ok)
        fprintf(stderr, "Error: failed to write ELF object '%s': %s\n", path,
                strerror(errno));
    asm_buf_free(&strtab);
    asm_buf_free(&shstrtab);
    free(syms);
    free(relas);
    return ok;
}
