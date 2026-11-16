#ifndef TRACEY_ASSEMBLER_INTERNAL_H
#define TRACEY_ASSEMBLER_INTERNAL_H

#include "tracey/assembler.h"
#include "tracey/elf.h"
#include <ctype.h>
#include <limits.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define ASM_NPOS ((size_t)-1)
#define ASM_BASE_ADDR ((uint64_t)0x401000)
#define ASM_MAX_OPERANDS 8
#define ASM_RELOC_CONDBR19 280


/*
 * Growable byte buffer used for the encoded text and for the DWARF sections.
 *
 * data owns the allocation (may be NULL while len is zero), cap is the
 * allocated size in bytes. failed latches an allocation failure so that
 * emission helpers can keep appending without checking every call; the
 * assembler tests the flag once before handing the buffer to the caller.
 */
struct asm_slice
{
    const char *p;
    size_t n;
};

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

struct asm_slice asm_make_slice(const char *p, size_t n);
struct asm_slice asm_slice_cstr(const char *s);
struct asm_slice asm_slice_sub(struct asm_slice s, size_t start, size_t count);
struct asm_slice asm_slice_trim(struct asm_slice s);
bool asm_slice_eq(struct asm_slice s, const char *lit);
bool asm_slice_eq_cstr(struct asm_slice s, const char *cstr);
bool asm_slice_starts(struct asm_slice s, const char *prefix);
size_t asm_slice_find_char(struct asm_slice s, char c);
size_t asm_slice_find_from(struct asm_slice s, char c, size_t from);
size_t asm_slice_rfind_char(struct asm_slice s, char c);
char *asm_slice_dup(struct asm_slice s);
struct asm_slice asm_after_char(struct asm_slice s, char c);
struct asm_slice asm_quoted_payload(struct asm_slice line, struct asm_slice fallback);
struct asm_slice asm_bracket_inside(struct asm_slice s, size_t lb, size_t rb);
bool asm_parse_int(struct asm_slice s, int *p_out);
bool asm_parse_filtered_int(struct asm_slice s, int *p_out);
int asm_regnum(struct asm_slice r);
int asm_condcode(struct asm_slice c);
void asm_buf_init(struct asm_buf *b);
void asm_buf_free(struct asm_buf *b);
bool asm_buf_reserve(struct asm_buf *b, size_t extra);
void asm_buf_byte(struct asm_buf *b, uint8_t v);
void asm_buf_bytes(struct asm_buf *b, const void *data, size_t len);
void asm_buf_u16(struct asm_buf *b, uint16_t v);
void asm_buf_u32(struct asm_buf *b, uint32_t v);
void asm_buf_u64(struct asm_buf *b, uint64_t v);
void asm_buf_uleb(struct asm_buf *b, uint64_t v);
void asm_buf_sleb(struct asm_buf *b, int64_t v);
void asm_buf_patch_u32(struct asm_buf *b, size_t pos, uint32_t v);
uint8_t *asm_buf_detach(struct asm_buf *b, size_t *p_len);
void asm_vec_init(struct asm_vec *v, size_t elem_size);
void *asm_vec_push(struct asm_vec *v);
void asm_set_error(struct tracey_asm_result *r, const char *fmt, ...);
void asm_result_release(struct tracey_asm_result *r);
void tracey_asm_result_free(struct tracey_asm_result *r);
size_t asm_split_operands(struct asm_slice operands, struct asm_slice *p_ops, size_t max_ops);
int asm_compare_labels(const void *p_a, const void *p_b);
int asm_compare_funcs(const void *p_a, const void *p_b);
bool asm_file_index(struct asm_vec *p_files, struct asm_slice name, int *p_index);
bool asm_label_set(struct asm_vec *p_labels, struct asm_slice name, size_t offset);
const struct asm_label *asm_label_find(const struct asm_vec *p_labels, struct asm_slice name);
size_t asm_escaped_length(struct asm_slice s);
enum asm_encode_status asm_fail_msg(struct asm_emit *e, const char *message);
enum asm_encode_status asm_fail_operands(struct asm_emit *e, const char *mnemonic);
void asm_emit_word(struct asm_emit *e, uint32_t word);
enum asm_encode_status asm_emit_branch(struct asm_emit *e, uint32_t insn,
                                      struct asm_slice symbol, int type);
enum asm_encode_status asm_encode_line(struct asm_emit *e, const char *mnemonic,
                                      struct asm_slice operands);
struct tracey_asm_result tracey_assemble_aarch64(const char *source, size_t source_len);
size_t asm_align8(size_t value);
bool asm_write_pad(FILE *f, size_t n);
size_t asm_strtab_add(struct asm_buf *b, const char *s);
bool tracey_write_elf_object(const char *path, const struct tracey_asm_result *r);

#endif /* TRACEY_ASSEMBLER_INTERNAL_H */
