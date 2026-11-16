#include "assembler_internal.h"


struct asm_slice asm_make_slice(const char *p, size_t n)
{
    struct asm_slice s;

    s.p = p;
    s.n = n;
    return s;
}

struct asm_slice asm_slice_cstr(const char *s)
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
struct asm_slice asm_slice_sub(struct asm_slice s, size_t start, size_t count)
{
    if (start > s.n)
        start = s.n;
    if (count > s.n - start)
        count = s.n - start;
    return asm_make_slice(s.p + start, count);
}

struct asm_slice asm_slice_trim(struct asm_slice s)
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

bool asm_slice_eq(struct asm_slice s, const char *lit)
{
    size_t n = strlen(lit);

    if (s.n != n)
        return false;
    if (n == 0)
        return true;
    return memcmp(s.p, lit, n) == 0;
}

/* Compare a slice against a NUL-terminated string of unknown length. */
bool asm_slice_eq_cstr(struct asm_slice s, const char *cstr)
{
    size_t n = strlen(cstr);

    if (s.n != n)
        return false;
    if (n == 0)
        return true;
    return memcmp(s.p, cstr, n) == 0;
}

bool asm_slice_starts(struct asm_slice s, const char *prefix)
{
    size_t n = strlen(prefix);

    if (s.n < n)
        return false;
    if (n == 0)
        return true;
    return memcmp(s.p, prefix, n) == 0;
}

size_t asm_slice_find_char(struct asm_slice s, char c)
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
size_t asm_slice_find_from(struct asm_slice s, char c, size_t from)
{
    size_t i;

    if (from >= s.n)
        return ASM_NPOS;
    for (i = from; i < s.n; i++)
        if (s.p[i] == c)
            return i;
    return ASM_NPOS;
}

size_t asm_slice_rfind_char(struct asm_slice s, char c)
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
char *asm_slice_dup(struct asm_slice s)
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
struct asm_slice asm_after_char(struct asm_slice s, char c)
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
struct asm_slice asm_quoted_payload(struct asm_slice line, struct asm_slice fallback)
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
struct asm_slice asm_bracket_inside(struct asm_slice s, size_t lb, size_t rb)
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
bool asm_parse_int(struct asm_slice s, int *p_out)
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
bool asm_parse_filtered_int(struct asm_slice s, int *p_out)
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
int asm_regnum(struct asm_slice r)
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
int asm_condcode(struct asm_slice c)
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

void asm_buf_init(struct asm_buf *b)
{
    b->data = NULL;
    b->len = 0;
    b->cap = 0;
    b->failed = false;
}

void asm_buf_free(struct asm_buf *b)
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
bool asm_buf_reserve(struct asm_buf *b, size_t extra)
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

void asm_buf_byte(struct asm_buf *b, uint8_t v)
{
    if (!asm_buf_reserve(b, 1))
        return;
    b->data[b->len++] = v;
}

void asm_buf_bytes(struct asm_buf *b, const void *data, size_t len)
{
    if (len == 0)
        return;
    if (!asm_buf_reserve(b, len))
        return;
    memcpy(b->data + b->len, data, len);
    b->len += len;
}

void asm_buf_u16(struct asm_buf *b, uint16_t v)
{
    if (!asm_buf_reserve(b, 2))
        return;
    b->data[b->len++] = (uint8_t)(v & 0xFFu);
    b->data[b->len++] = (uint8_t)((v >> 8) & 0xFFu);
}

void asm_buf_u32(struct asm_buf *b, uint32_t v)
{
    if (!asm_buf_reserve(b, 4))
        return;
    b->data[b->len++] = (uint8_t)(v & 0xFFu);
    b->data[b->len++] = (uint8_t)((v >> 8) & 0xFFu);
    b->data[b->len++] = (uint8_t)((v >> 16) & 0xFFu);
    b->data[b->len++] = (uint8_t)((v >> 24) & 0xFFu);
}

void asm_buf_u64(struct asm_buf *b, uint64_t v)
{
    int i;

    if (!asm_buf_reserve(b, 8))
        return;
    for (i = 0; i < 8; i++)
        b->data[b->len++] = (uint8_t)((v >> (8 * i)) & 0xFFu);
}

/* Append an unsigned LEB128 value, as required by the DWARF encodings. */
void asm_buf_uleb(struct asm_buf *b, uint64_t v)
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
void asm_buf_sleb(struct asm_buf *b, int64_t v)
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
void asm_buf_patch_u32(struct asm_buf *b, size_t pos, uint32_t v)
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
uint8_t *asm_buf_detach(struct asm_buf *b, size_t *p_len)
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

void asm_vec_init(struct asm_vec *v, size_t elem_size)
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
void *asm_vec_push(struct asm_vec *v)
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
void asm_set_error(struct tracey_asm_result *r, const char *fmt, ...)
{
    va_list ap;

    va_start(ap, fmt);
    vsnprintf(r->error, sizeof r->error, fmt, ap); /* flawfinder: ignore: format string controlled internally */
    va_end(ap);
    r->ok = false;
}

/*
 * asm_result_release - free everything owned by a result, keeping the text
 * of the diagnostic intact.
 *
 * Used by tracey_asm_result_free() and by the assembler's error path.
 */
void asm_result_release(struct tracey_asm_result *r)
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
size_t asm_split_operands(struct asm_slice operands, struct asm_slice *p_ops, size_t max_ops)
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
int asm_compare_labels(const void *p_a, const void *p_b)
{
    const struct asm_label *const *a = p_a;
    const struct asm_label *const *b = p_b;

    return strcmp((*a)->name, (*b)->name);
}

int asm_compare_funcs(const void *p_a, const void *p_b)
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
bool asm_file_index(struct asm_vec *p_files, struct asm_slice name, int *p_index)
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
bool asm_label_set(struct asm_vec *p_labels, struct asm_slice name, size_t offset)
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
const struct asm_label *asm_label_find(const struct asm_vec *p_labels, struct asm_slice name)
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
size_t asm_escaped_length(struct asm_slice s)
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
enum asm_encode_status asm_fail_msg(struct asm_emit *e, const char *message)
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
enum asm_encode_status asm_fail_operands(struct asm_emit *e, const char *mnemonic)
{
    asm_set_error(e->p_result, "Invalid operands for instruction: %s", mnemonic);
    return ASM_ENCODE_ERROR;
}

/* Append one instruction word at the current offset. */
void asm_emit_word(struct asm_emit *e, uint32_t word)
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
enum asm_encode_status asm_emit_branch(struct asm_emit *e, uint32_t insn,
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
