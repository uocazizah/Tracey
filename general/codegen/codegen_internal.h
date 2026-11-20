#ifndef TRACEY_CODEGEN_INTERNAL_H
#define TRACEY_CODEGEN_INTERNAL_H

#include "tracey/codegen.h"
#include "tracey/ast.h"
#include "tracey/runtime.h"

#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct tracey_local_var
{
    char *name;
    int size;
    bool is_array;
    enum tracey_type_kind type;
    int struct_id;
    struct tracey_func_sig *fn_sig;
};

struct tracey_strtab_entry
{
    char *value;
    int index;
};

struct tracey_string_builder
{
    char *data;
    size_t len;
    size_t cap;
};

struct tracey_codegen_ctx
{
    FILE *out;
    struct tracey_string_builder body;
    struct tracey_local_var *locals;
    size_t local_count;
    size_t local_cap;
    struct tracey_strtab_entry *strtab;
    size_t strtab_count;
    size_t strtab_cap;
    int label_counter;
    int frame_size;
    int module_id;
    /*
     * Break/continue target stacks. Invariant: every loop and switch
     * lowering saves the entry depth and restores it on all paths,
     * including allocation-failure paths, so partially lowered constructs
     * never leave stale labels. Each function also starts with both stacks
     * empty. Counts are the number of valid entries; capacity is allocated.
     */
    char **break_stack;
    size_t break_count;
    size_t break_cap;
    char **continue_stack;
    size_t continue_count;
    size_t continue_cap;
    const struct tracey_program *program;
};

void sb_init(struct tracey_string_builder *sb);
void sb_free(struct tracey_string_builder *sb);
void sb_reserve(struct tracey_string_builder *sb, size_t extra);
void sb_append(struct tracey_string_builder *sb, const char *s);
char *cg_strdup(const char *s);
int cg_get_str(struct tracey_codegen_ctx *ctx, const char *s);
int cg_find_local(struct tracey_codegen_ctx *ctx, const char *name);
int cg_local_offset(struct tracey_codegen_ctx *ctx, const char *name);
void cg_emit(struct tracey_codegen_ctx *ctx, const char *s);
void cg_emitf(struct tracey_codegen_ctx *ctx, const char *fmt, ...);
char *cg_new_label(struct tracey_codegen_ctx *ctx, const char *prefix);
int cg_infer_struct_id(struct tracey_codegen_ctx *ctx, const struct tracey_expr *e);
const struct tracey_struct_member *cg_member(struct tracey_codegen_ctx *ctx, int base_sid, const char *field);
enum tracey_type_kind cg_infer_type(struct tracey_codegen_ctx *ctx, const struct tracey_expr *e);
void cg_add_local(struct tracey_codegen_ctx *ctx, const char *name, int size, bool is_array,
                  enum tracey_type_kind type, int struct_id, struct tracey_func_sig *fn_sig);
void cg_collect_vars_stmt(const struct tracey_stmt *s, struct tracey_codegen_ctx *ctx);
void cg_emit_load_immediate(struct tracey_codegen_ctx *ctx, uint64_t bits);
void cg_gen_printf(struct tracey_codegen_ctx *ctx, const struct tracey_call_expr *call);
void cg_gen_addr(struct tracey_codegen_ctx *ctx, const struct tracey_expr *e);
size_t cg_type_size(struct tracey_codegen_ctx *ctx, enum tracey_type_kind type, int struct_id);
void cg_gen_expr(struct tracey_codegen_ctx *ctx, const struct tracey_expr *e);
void cg_gen_stmt(struct tracey_codegen_ctx *ctx, const struct tracey_stmt *s, const char *fn_name);
void cg_emit_string_table(struct tracey_codegen_ctx *ctx);

#endif /* TRACEY_CODEGEN_INTERNAL_H */
