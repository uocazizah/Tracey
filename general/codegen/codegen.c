#include "tracey/codegen.h"

#include "tracey/runtime.h"

#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*
 * AArch64 code generator.
 *
 * The generator is a direct, single-pass emitter. Expressions are evaluated
 * into x0; x1/x2/x3 and d0/d1/s0/s1 are used as temporaries. All locals live
 * in the frame addressed through x29, so nested expression evaluation is
 * stack based through explicit sp adjustments.
 *
 * The module id namespaces the generated string labels so that several
 * translation units can be assembled and linked into one program.
 */

struct tracey_local_var
{
    char *name;
    int size;
    bool is_array;
    enum tracey_type_kind type;
    /*
     * Struct definition index when type is TRACEY_TYPE_STRUCT, otherwise
     * -1. Used by member access to find member offsets.
     */
    int struct_id;
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
    int module_id;
    char **break_stack;
    size_t break_count;
    size_t break_cap;
    char **continue_stack;
    size_t continue_count;
    size_t continue_cap;
    /*
     * Borrowed pointer to the program being generated. It supplies the
     * struct definitions used to lay out aggregate locals and to resolve
     * member offsets. The pointer is valid only for the duration of
     * tracey_codegen_generate.
     */
    const struct tracey_program *program;
};

static void sb_init(struct tracey_string_builder *sb)
{
    sb->data = NULL;
    sb->len = 0;
    sb->cap = 0;
}

static void sb_free(struct tracey_string_builder *sb)
{
    free(sb->data);
    sb_init(sb);
}

static void sb_reserve(struct tracey_string_builder *sb, size_t extra)
{
    size_t need = sb->len + extra + 1;
    size_t cap;
    char *p;

    if (need <= sb->cap)
        return;
    cap = sb->cap ? sb->cap : 128;
    while (cap < need)
        cap *= 2;
    p = (char *)realloc(sb->data, cap);
    if (p == NULL)
    {
        /* The generator has no error channel; treat OOM as fatal. */
        abort();
    }
    sb->data = p;
    sb->cap = cap;
}

static void sb_append(struct tracey_string_builder *sb, const char *s)
{
    size_t n = strlen(s ? s : "");

    sb_reserve(sb, n);
    memcpy(sb->data + sb->len, s ? s : "", n);
    sb->len += n;
    sb->data[sb->len] = '\0';
}

static char *cg_strdup(const char *s)
{
    size_t n = strlen(s ? s : "");
    char *p = (char *)malloc(n + 1);

    if (p == NULL)
        return NULL;
    memcpy(p, s ? s : "", n + 1);
    return p;
}

/*
 * cg_get_str - intern a string literal and return its table index.
 *
 * The table is shared by every function in the module so that labels are
 * unique and identical literals are emitted once. Entries are owned by the
 * generation context and released by tracey_codegen_generate.
 */
static int cg_get_str(struct tracey_codegen_ctx *ctx, const char *s)
{
    size_t i;

    for (i = 0; i < ctx->strtab_count; i++)
    {
        if (strcmp(ctx->strtab[i].value, s) == 0)
            return ctx->strtab[i].index;
    }
    if (ctx->strtab_count == ctx->strtab_cap)
    {
        size_t cap = ctx->strtab_cap ? ctx->strtab_cap * 2 : 8;
        struct tracey_strtab_entry *p =
            (struct tracey_strtab_entry *)realloc(ctx->strtab, cap * sizeof(*p));

        if (p == NULL)
            abort();
        ctx->strtab = p;
        ctx->strtab_cap = cap;
    }
    ctx->strtab[ctx->strtab_count].value = cg_strdup(s);
    ctx->strtab[ctx->strtab_count].index = (int)ctx->strtab_count;
    if (ctx->strtab[ctx->strtab_count].value == NULL)
        abort();
    ctx->strtab_count++;
    return (int)(ctx->strtab_count - 1);
}

static int cg_find_local(struct tracey_codegen_ctx *ctx, const char *name)
{
    size_t i;

    for (i = 0; i < ctx->local_count; i++)
    {
        if (strcmp(ctx->locals[i].name, name) == 0)
            return (int)i;
    }
    return -1;
}

/*
 * cg_local_offset - return the frame offset of a local.
 *
 * Offsets are negative displacements from x29. The offset of a variable is
 * recomputed from the ordered local list so that it always matches the frame
 * layout computed before the function body is emitted. Unknown names map to
 * zero, matching the original generator's tolerant behaviour.
 */
static int cg_local_offset(struct tracey_codegen_ctx *ctx, const char *name)
{
    int cursor = 0;
    size_t i;

    for (i = 0; i < ctx->local_count; i++)
    {
        cursor += ctx->locals[i].size;
        if (cursor % 8 != 0)
            cursor += 8 - (cursor % 8);
        if (strcmp(ctx->locals[i].name, name) == 0)
            return -cursor;
    }
    return 0;
}

static void cg_emit(struct tracey_codegen_ctx *ctx, const char *s)
{
    sb_append(&ctx->body, "    ");
    sb_append(&ctx->body, s);
    sb_append(&ctx->body, "\n");
}

static void cg_emitf(struct tracey_codegen_ctx *ctx, const char *fmt, ...)
{
    char buf[1024];
    va_list ap;

    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap); /* flawfinder: ignore: format string controlled internally */
    va_end(ap);
    cg_emit(ctx, buf);
}

/*
 * cg_new_label - allocate a unique label name.
 *
 * Returns a heap-allocated label owned by the caller.
 */
static char *cg_new_label(struct tracey_codegen_ctx *ctx, const char *prefix)
{
    char buf[64];

    snprintf(buf, sizeof(buf), "%s%d", prefix, ctx->label_counter++);
    return cg_strdup(buf);
}

void tracey_codegen_init(struct tracey_code_generator *cg, enum tracey_target_arch arch)
{
    if (cg == NULL)
        return;
    cg->arch = arch;
}

static void cg_gen_expr(struct tracey_codegen_ctx *ctx, const struct tracey_expr *e);
static void cg_gen_stmt(struct tracey_codegen_ctx *ctx, const struct tracey_stmt *s, const char *fn_name);

static int cg_infer_struct_id(struct tracey_codegen_ctx *ctx, const struct tracey_expr *e);

/*
 * cg_infer_struct_id - resolve the struct id of an expression.
 *
 * Returns the struct definition index when e has struct value type, or -1
 * otherwise. Identifier results come from the local slot recorded during
 * variable collection; member results come from the member's own type in
 * the base struct. No diagnostics are produced.
 */
static int cg_infer_struct_id(struct tracey_codegen_ctx *ctx, const struct tracey_expr *e)
{
    if (e == NULL)
        return -1;

    if (e->kind == TRACEY_EXPR_IDENT)
    {
        int idx = cg_find_local(ctx, e->as.ident_expr.name);

        if (idx >= 0 && ctx->locals[idx].type == TRACEY_TYPE_STRUCT)
            return ctx->locals[idx].struct_id;
        return -1;
    }

    if (e->kind == TRACEY_EXPR_MEMBER)
    {
        int base_sid = cg_infer_struct_id(ctx, e->as.member_expr.base);
        const struct tracey_struct_def *def;
        size_t i;

        if (base_sid < 0 || ctx->program == NULL ||
            (size_t)base_sid >= ctx->program->struct_count)
            return -1;
        def = &ctx->program->structs[base_sid];
        for (i = 0; i < def->member_count; i++)
        {
            if (def->members[i].name != NULL &&
                strcmp(def->members[i].name, e->as.member_expr.field) == 0)
                return def->members[i].struct_id;
        }
    }
    return -1;
}

/*
 * cg_member - find a member in the struct selected by base_sid.
 *
 * Returns a borrowed pointer to the member, or NULL when the struct or the
 * member cannot be resolved.
 */
static const struct tracey_struct_member *
cg_member(struct tracey_codegen_ctx *ctx, int base_sid, const char *field)
{
    const struct tracey_struct_def *def;
    size_t i;

    if (ctx->program == NULL || base_sid < 0 || (size_t)base_sid >= ctx->program->struct_count)
        return NULL;
    def = &ctx->program->structs[base_sid];
    for (i = 0; i < def->member_count; i++)
    {
        if (def->members[i].name != NULL && strcmp(def->members[i].name, field) == 0)
            return &def->members[i];
    }
    return NULL;
}

static enum tracey_type_kind cg_infer_type(struct tracey_codegen_ctx *ctx,
                                           const struct tracey_expr *e)
{
    if (e == NULL)
        return TRACEY_TYPE_VOID;

    switch (e->kind)
    {
        case TRACEY_EXPR_INT:
            return TRACEY_TYPE_INT;
        case TRACEY_EXPR_BOOL:
            return TRACEY_TYPE_BOOL;
        case TRACEY_EXPR_REAL:
            return e->as.real_expr.is_float ? TRACEY_TYPE_FLOAT : TRACEY_TYPE_DOUBLE;
        case TRACEY_EXPR_STRING:
            return TRACEY_TYPE_CHAR_PTR;
        case TRACEY_EXPR_CHAR:
            return TRACEY_TYPE_CHAR;
        case TRACEY_EXPR_IDENT:
        {
            int idx = cg_find_local(ctx, e->as.ident_expr.name);
            if (idx >= 0)
                return ctx->locals[idx].type;
            return TRACEY_TYPE_INT;
        }
        case TRACEY_EXPR_INDEX:
        {
            enum tracey_type_kind bt = cg_infer_type(ctx, e->as.index_expr.base);
            if (bt == TRACEY_TYPE_INT_PTR)
                return TRACEY_TYPE_INT;
            if (bt == TRACEY_TYPE_CHAR_PTR)
                return TRACEY_TYPE_CHAR;
            if (bt == TRACEY_TYPE_FLOAT_PTR)
                return TRACEY_TYPE_FLOAT;
            if (bt == TRACEY_TYPE_DOUBLE_PTR)
                return TRACEY_TYPE_DOUBLE;
            if (bt == TRACEY_TYPE_LONG_PTR)
                return TRACEY_TYPE_LONG;
            if (bt == TRACEY_TYPE_LONG_LONG_PTR)
                return TRACEY_TYPE_LONG_LONG;
            if (bt == TRACEY_TYPE_SHORT_PTR)
                return TRACEY_TYPE_SHORT;
            if (bt == TRACEY_TYPE_SIZE_T_PTR)
                return TRACEY_TYPE_SIZE_T;
            return TRACEY_TYPE_INT;
        }
        case TRACEY_EXPR_ADDR:
        {
            enum tracey_type_kind t = cg_infer_type(ctx, e->as.addr_expr.operand);
            if (t == TRACEY_TYPE_INT)
                return TRACEY_TYPE_INT_PTR;
            if (t == TRACEY_TYPE_CHAR)
                return TRACEY_TYPE_CHAR_PTR;
            if (t == TRACEY_TYPE_SHORT)
                return TRACEY_TYPE_SHORT_PTR;
            if (t == TRACEY_TYPE_LONG)
                return TRACEY_TYPE_LONG_PTR;
            if (t == TRACEY_TYPE_LONG_LONG)
                return TRACEY_TYPE_LONG_LONG_PTR;
            if (t == TRACEY_TYPE_SIZE_T)
                return TRACEY_TYPE_SIZE_T_PTR;
            if (t == TRACEY_TYPE_FLOAT)
                return TRACEY_TYPE_FLOAT_PTR;
            if (t == TRACEY_TYPE_DOUBLE)
                return TRACEY_TYPE_DOUBLE_PTR;
            return TRACEY_TYPE_INT_PTR;
        }
        case TRACEY_EXPR_DEREF:
        {
            enum tracey_type_kind t = cg_infer_type(ctx, e->as.deref_expr.operand);
            if (t == TRACEY_TYPE_INT_PTR)
                return TRACEY_TYPE_INT;
            if (t == TRACEY_TYPE_CHAR_PTR)
                return TRACEY_TYPE_CHAR;
            if (t == TRACEY_TYPE_SHORT_PTR)
                return TRACEY_TYPE_SHORT;
            if (t == TRACEY_TYPE_LONG_PTR)
                return TRACEY_TYPE_LONG;
            if (t == TRACEY_TYPE_LONG_LONG_PTR)
                return TRACEY_TYPE_LONG_LONG;
            if (t == TRACEY_TYPE_SIZE_T_PTR)
                return TRACEY_TYPE_SIZE_T;
            if (t == TRACEY_TYPE_FLOAT_PTR)
                return TRACEY_TYPE_FLOAT;
            if (t == TRACEY_TYPE_DOUBLE_PTR)
                return TRACEY_TYPE_DOUBLE;
            return TRACEY_TYPE_INT;
        }
        case TRACEY_EXPR_UNARY:
            if (e->as.unary_expr.op == TRACEY_UNARY_LOG_NOT)
                return TRACEY_TYPE_BOOL;
            return cg_infer_type(ctx, e->as.unary_expr.operand);
            if (e->as.binary_expr.op == TRACEY_BINARY_ASSIGN)
                return cg_infer_type(ctx, e->as.binary_expr.left);
            switch (e->as.binary_expr.op)
            {
                case TRACEY_BINARY_EQ:
                case TRACEY_BINARY_NE:
                case TRACEY_BINARY_LT:
                case TRACEY_BINARY_GT:
                case TRACEY_BINARY_LE:
                case TRACEY_BINARY_GE:
                case TRACEY_BINARY_LOG_AND:
                case TRACEY_BINARY_LOG_OR:
                    return TRACEY_TYPE_BOOL;
                default:
                {
                    enum tracey_type_kind lt = cg_infer_type(ctx, e->as.binary_expr.left);
                    enum tracey_type_kind rt = cg_infer_type(ctx, e->as.binary_expr.right);
                    if (lt == TRACEY_TYPE_DOUBLE || rt == TRACEY_TYPE_DOUBLE)
                        return TRACEY_TYPE_DOUBLE;
                    if (lt == TRACEY_TYPE_FLOAT || rt == TRACEY_TYPE_FLOAT)
                        return TRACEY_TYPE_FLOAT;
                    return TRACEY_TYPE_INT;
                }
            }
        case TRACEY_EXPR_CALL:
            return TRACEY_TYPE_INT;
        case TRACEY_EXPR_TERNARY:
            return cg_infer_type(ctx, e->as.ternary_expr.then_branch);
        case TRACEY_EXPR_MEMBER:
        {
            int sid = cg_infer_struct_id(ctx, e);
            if (sid >= 0 && ctx->program != NULL && (size_t)sid < ctx->program->struct_count)
            {
                const struct tracey_struct_def *def = &ctx->program->structs[sid];
                size_t i;

                for (i = 0; i < def->member_count; i++)
                {
                    if (def->members[i].name != NULL &&
                        strcmp(def->members[i].name, e->as.member_expr.field) == 0)
                        return def->members[i].type;
                }
            }
            return TRACEY_TYPE_INT;
        }
        default:
            return TRACEY_TYPE_INT;
    }
}

/*
 * cg_add_local - register a local or parameter slot.
 *
 * Duplicate names are ignored; the first declaration determines the slot.
 * The stored name is a heap copy owned by the context.
 */
static void cg_add_local(struct tracey_codegen_ctx *ctx, const char *name, int size, bool is_array,
                         enum tracey_type_kind type, int struct_id)
{
    if (cg_find_local(ctx, name) >= 0)
        return;
    if (ctx->local_count == ctx->local_cap)
    {
        size_t cap = ctx->local_cap ? ctx->local_cap * 2 : 16;
        struct tracey_local_var *p =
            (struct tracey_local_var *)realloc(ctx->locals, cap * sizeof(*p));

        if (p == NULL)
            abort();
        ctx->locals = p;
        ctx->local_cap = cap;
    }
    ctx->locals[ctx->local_count].name = cg_strdup(name);
    if (ctx->locals[ctx->local_count].name == NULL)
        abort();
    ctx->locals[ctx->local_count].size = size;
    ctx->locals[ctx->local_count].is_array = is_array;
    ctx->locals[ctx->local_count].type = type;
    ctx->locals[ctx->local_count].struct_id = struct_id;
    ctx->local_count++;
}

/*
 * cg_collect_vars_stmt - collect locals declared inside a statement tree.
 *
 * Arrays reserve eight bytes per element and decay to the matching pointer
 * type, which is what the expression generator expects when it computes the
 * address of an identifier.
 */
static void cg_collect_vars_stmt(const struct tracey_stmt *s, struct tracey_codegen_ctx *ctx)
{
    size_t i;

    if (s == NULL)
        return;

    switch (s->kind)
    {
        case TRACEY_STMT_BLOCK:
            for (i = 0; i < s->as.block_stmt.count; i++)
                cg_collect_vars_stmt(s->as.block_stmt.statements[i], ctx);
            return;
        case TRACEY_STMT_VAR_DECL:
        {
            int sz = (s->as.var_decl_stmt.array_size > 0)
                         ? (int)(s->as.var_decl_stmt.array_size * 8)
                         : 8;
            enum tracey_type_kind t = s->as.var_decl_stmt.type;
            int sid = s->as.var_decl_stmt.struct_id;

            if (t == TRACEY_TYPE_STRUCT && ctx->program != NULL &&
                sid >= 0 && (size_t)sid < ctx->program->struct_count)
            {
                sz = (int)ctx->program->structs[sid].size;
                if (sz <= 0)
                    sz = 8;
            }
            else if (s->as.var_decl_stmt.array_size > 0)
            {
                if (t == TRACEY_TYPE_INT)
                    t = TRACEY_TYPE_INT_PTR;
                else if (t == TRACEY_TYPE_UNSIGNED_INT)
                    t = TRACEY_TYPE_INT_PTR;
                else if (t == TRACEY_TYPE_CHAR)
                    t = TRACEY_TYPE_CHAR_PTR;
                else if (t == TRACEY_TYPE_SHORT)
                    t = TRACEY_TYPE_SHORT_PTR;
                else if (t == TRACEY_TYPE_UNSIGNED_SHORT)
                    t = TRACEY_TYPE_SHORT_PTR;
                else if (t == TRACEY_TYPE_LONG)
                    t = TRACEY_TYPE_LONG_PTR;
                else if (t == TRACEY_TYPE_UNSIGNED_LONG)
                    t = TRACEY_TYPE_LONG_PTR;
                else if (t == TRACEY_TYPE_LONG_LONG)
                    t = TRACEY_TYPE_LONG_LONG_PTR;
                else if (t == TRACEY_TYPE_UNSIGNED_LONG_LONG)
                    t = TRACEY_TYPE_LONG_LONG_PTR;
                else if (t == TRACEY_TYPE_SIZE_T)
                    t = TRACEY_TYPE_SIZE_T_PTR;
                else if (t == TRACEY_TYPE_FLOAT)
                    t = TRACEY_TYPE_FLOAT_PTR;
                else if (t == TRACEY_TYPE_DOUBLE)
                    t = TRACEY_TYPE_DOUBLE_PTR;
            }
            cg_add_local(ctx, s->as.var_decl_stmt.name, sz,
                         s->as.var_decl_stmt.array_size > 0, t, sid);
            return;
        }
        case TRACEY_STMT_IF:
            cg_collect_vars_stmt(s->as.if_stmt.then_branch, ctx);
            cg_collect_vars_stmt(s->as.if_stmt.else_branch, ctx);
            return;
        case TRACEY_STMT_DO_WHILE:
            cg_collect_vars_stmt(s->as.do_while_stmt.body, ctx);
            return;
        case TRACEY_STMT_SWITCH:
            for (size_t _ci = 0; _ci < s->as.switch_stmt.case_count; _ci++)
                cg_collect_vars_stmt(s->as.switch_stmt.cases[_ci].body, ctx);
            cg_collect_vars_stmt(s->as.switch_stmt.default_body, ctx);
            return;
        case TRACEY_STMT_WHILE:
            cg_collect_vars_stmt(s->as.while_stmt.body, ctx);
            return;
        case TRACEY_STMT_FOR:
            cg_collect_vars_stmt(s->as.for_stmt.init, ctx);
            cg_collect_vars_stmt(s->as.for_stmt.body, ctx);
            return;
        default:
            return;
    }
}

/*
 * cg_emit_load_immediate - load a full 64-bit immediate into x0.
 *
 * AArch64 "mov x0, #imm" only accepts a 16-bit immediate, so the value is
 * built with movz/movk. Negative integers are loaded through their
 * two's-complement bit pattern.
 */
static void cg_emit_load_immediate(struct tracey_codegen_ctx *ctx, uint64_t bits)
{
    cg_emitf(ctx, "mov x0, #%llu", (unsigned long long)(bits & 0xFFFFULL));
    cg_emitf(ctx, "movk x0, #%llu, lsl #16", (unsigned long long)((bits >> 16) & 0xFFFFULL));
    cg_emitf(ctx, "movk x0, #%llu, lsl #32", (unsigned long long)((bits >> 32) & 0xFFFFULL));
    cg_emitf(ctx, "movk x0, #%llu, lsl #48", (unsigned long long)((bits >> 48) & 0xFFFFULL));
}

/*
 * cg_gen_printf - lower a printf call with a literal format string.
 *
 * Text segments are interned and printed with the string helper. %d and %s
 * consume one call argument each; %% emits a literal percent sign. A format
 * string that is not a literal is ignored, matching the original generator.
 */
static void cg_gen_printf(struct tracey_codegen_ctx *ctx, const struct tracey_call_expr *call)
{
    const char *fmt;
    size_t argi = 1;
    char *seg;
    size_t seg_cap;
    size_t seg_len = 0;
    size_t i = 0;
    char prefix[64];

    if (call->arg_count == 0 || call->args[0]->kind != TRACEY_EXPR_STRING)
        return;

    fmt = call->args[0]->as.string_expr.value ? call->args[0]->as.string_expr.value : "";
    seg_cap = strlen(fmt) + 1;
    seg = (char *)malloc(seg_cap);
    if (seg == NULL)
        abort();
    seg[0] = '\0';

    snprintf(prefix, sizeof(prefix), "L%dstr", ctx->module_id);

    while (i < strlen(fmt))
    {
        char c = fmt[i];

        if (c == '%')
        {
            if (seg_len > 0)
            {
                int idx = cg_get_str(ctx, seg);

                cg_emitf(ctx, "adr x0, %s%d", prefix, idx);
                cg_emit(ctx, "bl __tracey_print_str");
                seg_len = 0;
                seg[0] = '\0';
            }
            if (i + 1 < strlen(fmt))
            {
                char spec = fmt[i + 1];
                i++;
                if (spec == 'd')
                {
                    if (argi < call->arg_count)
                        cg_gen_expr(ctx, call->args[argi++]);
                    cg_emit(ctx, "bl __tracey_print_int");
                }
                else if (spec == 's')
                {
                    if (argi < call->arg_count)
                        cg_gen_expr(ctx, call->args[argi++]);
                    cg_emit(ctx, "bl __tracey_print_str");
                }
                else if (spec == '%')
                {
                    seg[seg_len++] = '%';
                    seg[seg_len] = '\0';
                }
            }
        }
        else
        {
            seg[seg_len++] = c;
            seg[seg_len] = '\0';
        }
        i++;
    }

    if (seg_len > 0)
    {
        int idx = cg_get_str(ctx, seg);

        cg_emitf(ctx, "adr x0, %s%d", prefix, idx);
        cg_emit(ctx, "bl __tracey_print_str");
    }
    free(seg);
}

/*
 * cg_gen_addr - compute the address of an lvalue into x0.
 *
 * Handles identifiers, member access, subscript and dereference. Arrays
 * yield the base address of their frame slot. The result is a byte address
 * in x0; callers load or store through it. This separation is what allows
 * struct members to be used as both rvalues and assignment targets.
 */
static void cg_gen_addr(struct tracey_codegen_ctx *ctx, const struct tracey_expr *e)
{
    if (e == NULL)
        return;

    switch (e->kind)
    {
        case TRACEY_EXPR_IDENT:
        {
            int o = cg_local_offset(ctx, e->as.ident_expr.name);

            if (o < 0)
                cg_emitf(ctx, "sub x0, x29, #%d", -o);
            else
                cg_emitf(ctx, "add x0, x29, #%d", o);
            return;
        }
        case TRACEY_EXPR_MEMBER:
        {
            int base_sid = cg_infer_struct_id(ctx, e->as.member_expr.base);
            const struct tracey_struct_member *m =
                cg_member(ctx, base_sid, e->as.member_expr.field);
            size_t off = (m != NULL) ? m->offset : 0;

            cg_gen_addr(ctx, e->as.member_expr.base);
            if (off != 0)
                cg_emitf(ctx, "add x0, x0, #%zu", off);
            return;
        }
        case TRACEY_EXPR_INDEX:
        {
            enum tracey_type_kind bt = cg_infer_type(ctx, e->as.index_expr.base);
            int stride = (bt == TRACEY_TYPE_CHAR_PTR) ? 1 : 8;

            cg_gen_expr(ctx, e->as.index_expr.base);
            cg_emit(ctx, "sub sp, sp, #16");
            cg_emit(ctx, "str x0, [sp]");
            cg_gen_expr(ctx, e->as.index_expr.index);
            cg_emit(ctx, "mov x1, x0");
            cg_emit(ctx, "ldr x0, [sp]");
            cg_emit(ctx, "add sp, sp, #16");
            cg_emitf(ctx, "mov x2, #%d", stride);
            cg_emit(ctx, "mul x3, x1, x2");
            cg_emit(ctx, "add x0, x0, x3");
            return;
        }
        case TRACEY_EXPR_DEREF:
            cg_gen_expr(ctx, e->as.deref_expr.operand);
            return;
        default:
            cg_gen_expr(ctx, e);
            return;
    }
}

static void cg_gen_expr(struct tracey_codegen_ctx *ctx, const struct tracey_expr *e)
{
    if (e == NULL)
        return;

    switch (e->kind)
    {
        case TRACEY_EXPR_INT:
        {
            uint64_t bits = (uint64_t)e->as.int_expr.value;

            cg_emit_load_immediate(ctx, bits);
            return;
        }
        case TRACEY_EXPR_BOOL:
            cg_emitf(ctx, "mov x0, #%d", e->as.bool_expr.value ? 1 : 0);
            return;
        case TRACEY_EXPR_CHAR:
            cg_emitf(ctx, "mov x0, #%d", e->as.char_expr.value);
            return;
        case TRACEY_EXPR_REAL:
        {
            uint64_t bits = 0;

            if (e->as.real_expr.is_float)
            {
                float fv = (float)e->as.real_expr.value;
                uint32_t b = 0;

                memcpy(&b, &fv, sizeof(b));
                bits = (uint64_t)b;
            }
            else
            {
                memcpy(&bits, &e->as.real_expr.value, sizeof(bits));
            }
            cg_emit_load_immediate(ctx, bits);
            return;
        }
        case TRACEY_EXPR_STRING:
        {
            int idx = cg_get_str(ctx, e->as.string_expr.value ? e->as.string_expr.value : "");

            cg_emitf(ctx, "adr x0, L%dstr%d", ctx->module_id, idx);
            return;
        }
        case TRACEY_EXPR_IDENT:
        {
            int li = cg_find_local(ctx, e->as.ident_expr.name);
            int o = cg_local_offset(ctx, e->as.ident_expr.name);
            bool is_array = (li >= 0 && ctx->locals[li].is_array);

            if (is_array)
            {
                if (o < 0)
                    cg_emitf(ctx, "sub x0, x29, #%d", -o);
                else
                    cg_emitf(ctx, "add x0, x29, #%d", o);
            }
            else
            {
                cg_emitf(ctx, "ldr x0, [x29, #%d]", o);
            }
            return;
        }
        case TRACEY_EXPR_INDEX:
        {
            enum tracey_type_kind bt = cg_infer_type(ctx, e->as.index_expr.base);
            int stride = 8;

            if (bt == TRACEY_TYPE_CHAR_PTR)
                stride = 1;
            cg_gen_expr(ctx, e->as.index_expr.base);
            cg_emit(ctx, "sub sp, sp, #16");
            cg_emit(ctx, "str x0, [sp]");
            cg_gen_expr(ctx, e->as.index_expr.index);
            cg_emit(ctx, "mov x1, x0");
            cg_emit(ctx, "ldr x0, [sp]");
            cg_emit(ctx, "add sp, sp, #16");
            cg_emitf(ctx, "mov x2, #%d", stride);
            cg_emit(ctx, "mul x3, x1, x2");
            cg_emit(ctx, "add x0, x0, x3");
            if (bt == TRACEY_TYPE_CHAR_PTR)
                cg_emit(ctx, "ldrb w0, [x0]");
            else
                cg_emit(ctx, "ldr x0, [x0]");
            return;
        }
        case TRACEY_EXPR_ADDR:
        {
            if (e->as.addr_expr.operand->kind == TRACEY_EXPR_IDENT)
            {
                int o = cg_local_offset(ctx, e->as.addr_expr.operand->as.ident_expr.name);

                if (o < 0)
                    cg_emitf(ctx, "sub x0, x29, #%d", -o);
                else
                    cg_emitf(ctx, "add x0, x29, #%d", o);
                return;
            }
            cg_gen_expr(ctx, e->as.addr_expr.operand);
            return;
        }
        case TRACEY_EXPR_DEREF:
        {
            enum tracey_type_kind pt = cg_infer_type(ctx, e->as.deref_expr.operand);

            cg_gen_expr(ctx, e->as.deref_expr.operand);
            if (pt == TRACEY_TYPE_CHAR_PTR)
            {
                /* Bytes are zero-extended so the runtime can treat the
                 * result as an unsigned integer. */
                cg_emit(ctx, "ldrb w0, [x0]");
            }
            else
            {
                cg_emit(ctx, "ldr x0, [x0]");
            }
            return;
        }
        case TRACEY_EXPR_UNARY:
        {
            cg_gen_expr(ctx, e->as.unary_expr.operand);
            if (e->as.unary_expr.op == TRACEY_UNARY_NEG)
            {
                enum tracey_type_kind t = cg_infer_type(ctx, e->as.unary_expr.operand);

                if (t == TRACEY_TYPE_DOUBLE)
                {
                    cg_emit(ctx, "fmov d0, x0");
                    cg_emit(ctx, "fneg d0, d0");
                    cg_emit(ctx, "fmov x0, d0");
                }
                else if (t == TRACEY_TYPE_FLOAT)
                {
                    cg_emit(ctx, "fmov s0, w0");
                    cg_emit(ctx, "fneg s0, s0");
                    cg_emit(ctx, "fmov w0, s0");
                }
                else
                {
                    cg_emit(ctx, "neg x0, x0");
                }
            }
            else if (e->as.unary_expr.op == TRACEY_UNARY_LOG_NOT)
            {
                cg_emit(ctx, "cmp x0, #0");
                cg_emit(ctx, "cset x0, eq");
            }
            else if (e->as.unary_expr.op == TRACEY_UNARY_PRE_INC ||
                     e->as.unary_expr.op == TRACEY_UNARY_PRE_DEC ||
                     e->as.unary_expr.op == TRACEY_UNARY_POST_INC ||
                     e->as.unary_expr.op == TRACEY_UNARY_POST_DEC)
            {
                bool is_inc = (e->as.unary_expr.op == TRACEY_UNARY_PRE_INC ||
                               e->as.unary_expr.op == TRACEY_UNARY_POST_INC);
                bool is_post = (e->as.unary_expr.op == TRACEY_UNARY_POST_INC ||
                                e->as.unary_expr.op == TRACEY_UNARY_POST_DEC);
                struct tracey_expr *op = e->as.unary_expr.operand;
                int stride = 8;
                if (op->kind == TRACEY_EXPR_IDENT)
                {
                    /* address = x29 + offset */
                }
                else if (op->kind == TRACEY_EXPR_INDEX)
                {
                    /* handled below generically via address eval */
                }

                if (op->kind == TRACEY_EXPR_IDENT)
                {
                    int o = cg_local_offset(ctx, op->as.ident_expr.name);
                    /* load old */
                    cg_emitf(ctx, "ldr x1, [x29, #%d]", o);
                    cg_emit(ctx, "str x1, [sp]"); /* save old on temp stack */
                    /* compute new */
                    cg_emit(ctx, "mov x0, x1");
                    if (is_inc)
                        cg_emit(ctx, "add x0, x0, #1");
                    else
                        cg_emit(ctx, "sub x0, x0, #1");
                    /* store new */
                    cg_emitf(ctx, "str x0, [x29, #%d]", o);
                    if (is_post)
                        cg_emit(ctx, "ldr x0, [sp]"); /* old value */
                    /* cleanup (no sp change needed) */
                }
                else
                {
                    /* Fallback: evaluate address then indirect (index/deref) */
                    if (op->kind == TRACEY_EXPR_DEREF)
                    {
                        cg_gen_expr(ctx, op->as.deref_expr.operand);
                    }
                    else if (op->kind == TRACEY_EXPR_INDEX)
                    {
                        struct tracey_expr *base = op->as.index_expr.base;
                        struct tracey_expr *idx = op->as.index_expr.index;
                        enum tracey_type_kind bt = cg_infer_type(ctx, base);
                        stride = (bt == TRACEY_TYPE_CHAR_PTR) ? 1 : 8;
                        cg_gen_expr(ctx, base);
                        cg_emit(ctx, "sub sp, sp, #16");
                        cg_emit(ctx, "str x0, [sp]");
                        cg_gen_expr(ctx, idx);
                        cg_emit(ctx, "mov x1, x0");
                        cg_emit(ctx, "ldr x0, [sp]");
                        cg_emit(ctx, "add sp, sp, #16");
                        cg_emitf(ctx, "mov x2, #%d", stride);
                        cg_emit(ctx, "mul x1, x1, x2");
                        cg_emit(ctx, "add x0, x0, x1");
                    }
                    cg_emit(ctx, "sub sp, sp, #32");
                    cg_emit(ctx, "str x0, [sp]");       /* addr */
                    cg_emit(ctx, "ldr x1, [sp]");
                    cg_emit(ctx, "ldr x1, [x1]");       /* old value */
                    cg_emit(ctx, "str x1, [sp, #8]");   /* save old */
                    cg_emit(ctx, "ldr x0, [sp, #8]");
                    if (is_inc)
                        cg_emit(ctx, "add x0, x0, #1");
                    else
                        cg_emit(ctx, "sub x0, x0, #1");
                    cg_emit(ctx, "ldr x1, [sp]");       /* addr */
                    cg_emit(ctx, "str x0, [x1]");       /* store new */
                    if (is_post)
                        cg_emit(ctx, "ldr x0, [sp, #8]"); /* old value */
                    cg_emit(ctx, "add sp, sp, #32");
                }
            }
            return;
        }
        case TRACEY_EXPR_CALL:
        {
            const char *callee = e->as.call_expr.callee ? e->as.call_expr.callee : "";
            size_t i;

            /*
             * Dispatch the standard-library builtins before falling through
             * to a general call. Each branch must stand on its own; a nested
             * or dangling 'if' here would silently skip the builtin and emit
             * a call to an unresolved symbol at link time.
             */
            if (strcmp(callee, "input_int") == 0)
            {
                cg_emit(ctx, "bl __tracey_input_int");
                return;
            }
            if (strcmp(callee, "print") == 0)
            {
                bool is_string_arg = false;

                if (e->as.call_expr.arg_count > 0)
                {
                    const struct tracey_expr *a = e->as.call_expr.args[0];

                    cg_gen_expr(ctx, a);
                    if (a->kind == TRACEY_EXPR_STRING)
                        is_string_arg = true;
                    else if (cg_infer_type(ctx, a) == TRACEY_TYPE_CHAR_PTR)
                        is_string_arg = true;
                }
                cg_emit(ctx, is_string_arg ? "bl __tracey_print_str" : "bl __tracey_print_int");
                return;
            }
            if (strcmp(callee, "input_str") == 0)
            {
                if (e->as.call_expr.arg_count >= 2)
                {
                    cg_gen_expr(ctx, e->as.call_expr.args[0]);
                    cg_emit(ctx, "sub sp, sp, #16");
                    cg_emit(ctx, "str x0, [sp]");
                    cg_gen_expr(ctx, e->as.call_expr.args[1]);
                    cg_emit(ctx, "mov x1, x0");
                    cg_emit(ctx, "ldr x0, [sp]");
                    cg_emit(ctx, "add sp, sp, #16");
                }
                cg_emit(ctx, "bl __tracey_input_str");
                return;
            }
            if (strcmp(callee, "printf") == 0)
            {
                cg_gen_printf(ctx, &e->as.call_expr);
                return;
            }

            if (strcmp(callee, "puts") == 0)
            {
                if (e->as.call_expr.arg_count > 0)
                    cg_gen_expr(ctx, e->as.call_expr.args[0]);
                cg_emit(ctx, "bl __tracey_puts");
                return;
            }
            if (strcmp(callee, "putchar") == 0)
            {
                if (e->as.call_expr.arg_count > 0)
                    cg_gen_expr(ctx, e->as.call_expr.args[0]);
                cg_emit(ctx, "bl __tracey_putchar");
                return;
            }
            if (strcmp(callee, "getchar") == 0)
            {
                cg_emit(ctx, "bl __tracey_getchar");
                return;
            }
            if (strcmp(callee, "fget") == 0)
            {
                if (e->as.call_expr.arg_count > 0)
                    cg_gen_expr(ctx, e->as.call_expr.args[0]);
                cg_emit(ctx, "bl __tracey_fget");
                return;
            }
            if (strcmp(callee, "scanf") == 0)
            {
                size_t ai;
                for (ai = 0; ai < e->as.call_expr.arg_count; ai++)
                {
                    cg_gen_expr(ctx, e->as.call_expr.args[ai]);
                    cg_emit(ctx, "sub sp, sp, #16");
                    cg_emit(ctx, "str x0, [sp]");
                }
                for (ai = e->as.call_expr.arg_count; ai > 0; ai--)
                {
                    cg_emitf(ctx, "ldr x%zu, [sp]", ai - 1);
                    cg_emit(ctx, "add sp, sp, #16");
                }
                cg_emit(ctx, "bl __tracey_scanf");
                return;
            }
            if (strcmp(callee, "scanfs") == 0)
            {
                size_t ai;
                for (ai = 0; ai < e->as.call_expr.arg_count; ai++)
                {
                    cg_gen_expr(ctx, e->as.call_expr.args[ai]);
                    cg_emit(ctx, "sub sp, sp, #16");
                    cg_emit(ctx, "str x0, [sp]");
                }
                for (ai = e->as.call_expr.arg_count; ai > 0; ai--)
                {
                    cg_emitf(ctx, "ldr x%zu, [sp]", ai - 1);
                    cg_emit(ctx, "add sp, sp, #16");
                }
                cg_emit(ctx, "bl __tracey_scanfs");
                return;
            }
            if (strcmp(callee, "fgets") == 0)
            {
                if (e->as.call_expr.arg_count >= 2)
                {
                    cg_gen_expr(ctx, e->as.call_expr.args[0]);
                    cg_emit(ctx, "sub sp, sp, #16");
                    cg_emit(ctx, "str x0, [sp]");
                    cg_gen_expr(ctx, e->as.call_expr.args[1]);
                    cg_emit(ctx, "mov x1, x0");
                    cg_emit(ctx, "ldr x0, [sp]");
                    cg_emit(ctx, "add sp, sp, #16");
                }
                cg_emit(ctx, "bl __tracey_input_str");
                return;
            }

            /* General call: evaluate arguments left to right, push each one,
             * then pop them in reverse so x0 holds the first argument. */
            for (i = 0; i < e->as.call_expr.arg_count; i++)
            {
                cg_gen_expr(ctx, e->as.call_expr.args[i]);
                cg_emit(ctx, "sub sp, sp, #16");
                cg_emit(ctx, "str x0, [sp]");
            }
            for (i = e->as.call_expr.arg_count; i > 0; i--)
            {
                cg_emitf(ctx, "ldr x%zu, [sp]", i - 1);
                cg_emit(ctx, "add sp, sp, #16");
            }
            cg_emitf(ctx, "bl %s", callee);
            return;
        }
        case TRACEY_EXPR_TERNARY:
        {
            char *elseL = cg_new_label(ctx, ".LternaryElse");
            char *endL = cg_new_label(ctx, ".LternaryEnd");

            cg_gen_expr(ctx, e->as.ternary_expr.condition);
            cg_emit(ctx, "cmp x0, #0");
            cg_emitf(ctx, "beq %s", elseL);
            cg_gen_expr(ctx, e->as.ternary_expr.then_branch);
            cg_emitf(ctx, "b %s", endL);
            cg_emitf(ctx, "%s:", elseL);
            cg_gen_expr(ctx, e->as.ternary_expr.else_branch);
            cg_emitf(ctx, "%s:", endL);
            free(elseL);
            free(endL);
            return;
        }
        case TRACEY_EXPR_MEMBER:
            /*
             * Every struct member occupies eight bytes in this stage, so a
             * single 64-bit load retrieves the value regardless of the
             * declared member type.
             */
            cg_gen_addr(ctx, e);
            cg_emit(ctx, "ldr x0, [x0]");
            return;
        case TRACEY_EXPR_BINARY:
        {
            if (e->as.binary_expr.op == TRACEY_BINARY_ASSIGN)
            {
                if (e->as.binary_expr.left->kind == TRACEY_EXPR_IDENT)
                {
                    int o = cg_local_offset(ctx, e->as.binary_expr.left->as.ident_expr.name);

                    cg_gen_expr(ctx, e->as.binary_expr.right);
                    cg_emitf(ctx, "str x0, [x29, #%d]", o);
                    return;
                }
                if (e->as.binary_expr.left->kind == TRACEY_EXPR_INDEX)
                {
                    const struct tracey_expr *ix = e->as.binary_expr.left;

                    cg_gen_expr(ctx, ix->as.index_expr.base);
                    cg_emit(ctx, "sub sp, sp, #16");
                    cg_emit(ctx, "str x0, [sp]");
                    cg_gen_expr(ctx, ix->as.index_expr.index);
                    cg_emit(ctx, "mov x1, x0");
                    cg_emit(ctx, "ldr x0, [sp]");
                    cg_emit(ctx, "add sp, sp, #16");
                    cg_emit(ctx, "mov x2, #8");
                    cg_emit(ctx, "mul x3, x1, x2");
                    cg_emit(ctx, "add x0, x0, x3");
                    cg_emit(ctx, "sub sp, sp, #16");
                    cg_emit(ctx, "str x0, [sp]");
                    cg_gen_expr(ctx, e->as.binary_expr.right);
                    cg_emit(ctx, "ldr x1, [sp]");
                    cg_emit(ctx, "add sp, sp, #16");
                    cg_emit(ctx, "str x0, [x1]");
                    return;
                }
                if (e->as.binary_expr.left->kind == TRACEY_EXPR_DEREF)
                {
                    cg_gen_expr(ctx, e->as.binary_expr.left->as.deref_expr.operand);
                    cg_emit(ctx, "sub sp, sp, #16");
                    cg_emit(ctx, "str x0, [sp]");
                    cg_gen_expr(ctx, e->as.binary_expr.right);
                    cg_emit(ctx, "ldr x1, [sp]");
                    cg_emit(ctx, "add sp, sp, #16");
                    cg_emit(ctx, "str x0, [x1]");
                    return;
                }
                if (e->as.binary_expr.left->kind == TRACEY_EXPR_MEMBER)
                {
                    cg_gen_addr(ctx, e->as.binary_expr.left);
                    cg_emit(ctx, "sub sp, sp, #16");
                    cg_emit(ctx, "str x0, [sp]");
                    cg_gen_expr(ctx, e->as.binary_expr.right);
                    cg_emit(ctx, "ldr x1, [sp]");
                    cg_emit(ctx, "add sp, sp, #16");
                    cg_emit(ctx, "str x0, [x1]");
                    return;
                }
                return;
            }

            /* Compound assignment: load current value, apply op, store back. */
            if (e->as.binary_expr.op == TRACEY_BINARY_ADD_ASSIGN ||
                e->as.binary_expr.op == TRACEY_BINARY_SUB_ASSIGN ||
                e->as.binary_expr.op == TRACEY_BINARY_MUL_ASSIGN ||
                e->as.binary_expr.op == TRACEY_BINARY_DIV_ASSIGN ||
                e->as.binary_expr.op == TRACEY_BINARY_MOD_ASSIGN ||
                e->as.binary_expr.op == TRACEY_BINARY_SHL_ASSIGN ||
                e->as.binary_expr.op == TRACEY_BINARY_SHR_ASSIGN ||
                e->as.binary_expr.op == TRACEY_BINARY_AND_ASSIGN ||
                e->as.binary_expr.op == TRACEY_BINARY_OR_ASSIGN ||
                e->as.binary_expr.op == TRACEY_BINARY_XOR_ASSIGN)
            {
                if (e->as.binary_expr.left->kind == TRACEY_EXPR_IDENT)
                {
                    int o = cg_local_offset(ctx, e->as.binary_expr.left->as.ident_expr.name);
                    cg_emitf(ctx, "ldr x0, [x29, #%d]", o);
                    cg_emit(ctx, "sub sp, sp, #16");
                    cg_emit(ctx, "str x0, [sp]");
                    cg_gen_expr(ctx, e->as.binary_expr.right);
                    cg_emit(ctx, "mov x1, x0");
                    cg_emit(ctx, "ldr x0, [sp]");
                    cg_emit(ctx, "add sp, sp, #16");
                    switch (e->as.binary_expr.op)
                    {
                        case TRACEY_BINARY_ADD_ASSIGN: cg_emit(ctx, "add x0, x0, x1"); break;
                        case TRACEY_BINARY_SUB_ASSIGN: cg_emit(ctx, "sub x0, x0, x1"); break;
                        case TRACEY_BINARY_MUL_ASSIGN: cg_emit(ctx, "mul x0, x0, x1"); break;
                        case TRACEY_BINARY_DIV_ASSIGN: cg_emit(ctx, "sdiv x0, x0, x1"); break;
                        case TRACEY_BINARY_MOD_ASSIGN:
                            cg_emit(ctx, "sdiv x9, x0, x1");
                            cg_emit(ctx, "msub x0, x9, x1, x0");
                            break;
                        case TRACEY_BINARY_SHL_ASSIGN: cg_emit(ctx, "lsl x0, x0, x1"); break;
                        case TRACEY_BINARY_SHR_ASSIGN: cg_emit(ctx, "lsr x0, x0, x1"); break;
                        case TRACEY_BINARY_AND_ASSIGN: cg_emit(ctx, "and x0, x0, x1"); break;
                        case TRACEY_BINARY_OR_ASSIGN: cg_emit(ctx, "orr x0, x0, x1"); break;
                        case TRACEY_BINARY_XOR_ASSIGN: cg_emit(ctx, "eor x0, x0, x1"); break;
                        default: break;
                    }
                    cg_emitf(ctx, "str x0, [x29, #%d]", o);
                    return;
                }
                return;
            }

            cg_gen_expr(ctx, e->as.binary_expr.left);
            cg_emit(ctx, "sub sp, sp, #16");
            cg_emit(ctx, "str x0, [sp]");
            cg_gen_expr(ctx, e->as.binary_expr.right);
            cg_emit(ctx, "mov x1, x0");
            cg_emit(ctx, "ldr x0, [sp]");
            cg_emit(ctx, "add sp, sp, #16");

            {
                enum tracey_type_kind ltt = cg_infer_type(ctx, e->as.binary_expr.left);
                enum tracey_type_kind rtt = cg_infer_type(ctx, e->as.binary_expr.right);
                bool floating = (ltt == TRACEY_TYPE_FLOAT || ltt == TRACEY_TYPE_DOUBLE ||
                                 rtt == TRACEY_TYPE_FLOAT || rtt == TRACEY_TYPE_DOUBLE);
                bool use_double = (ltt == TRACEY_TYPE_DOUBLE || rtt == TRACEY_TYPE_DOUBLE);

                if (floating)
                {
                    if (use_double)
                    {
                        cg_emit(ctx, "fmov d0, x0");
                        cg_emit(ctx, "fmov d1, x1");
                    }
                    else
                    {
                        cg_emit(ctx, "fmov s0, w0");
                        cg_emit(ctx, "fmov s1, w1");
                    }

                    switch (e->as.binary_expr.op)
                    {
                        case TRACEY_BINARY_ADD:
                            cg_emit(ctx, use_double ? "fadd d0, d0, d1" : "fadd s0, s0, s1");
                            break;
                        case TRACEY_BINARY_SUB:
                            cg_emit(ctx, use_double ? "fsub d0, d0, d1" : "fsub s0, s0, s1");
                            break;
                        case TRACEY_BINARY_MUL:
                            cg_emit(ctx, use_double ? "fmul d0, d0, d1" : "fmul s0, s0, s1");
                            break;
                        case TRACEY_BINARY_DIV:
                            cg_emit(ctx, use_double ? "fdiv d0, d0, d1" : "fdiv s0, s0, s1");
                            break;
                        case TRACEY_BINARY_EQ:
                            cg_emit(ctx, use_double ? "fcmp d0, d1" : "fcmp s0, s1");
                            cg_emit(ctx, "cset x0, eq");
                            break;
                        case TRACEY_BINARY_NE:
                            cg_emit(ctx, use_double ? "fcmp d0, d1" : "fcmp s0, s1");
                            cg_emit(ctx, "cset x0, ne");
                            break;
                        case TRACEY_BINARY_LT:
                            cg_emit(ctx, use_double ? "fcmp d0, d1" : "fcmp s0, s1");
                            cg_emit(ctx, "cset x0, lt");
                            break;
                        case TRACEY_BINARY_GT:
                            cg_emit(ctx, use_double ? "fcmp d0, d1" : "fcmp s0, s1");
                            cg_emit(ctx, "cset x0, gt");
                            break;
                        case TRACEY_BINARY_LE:
                            cg_emit(ctx, use_double ? "fcmp d0, d1" : "fcmp s0, s1");
                            cg_emit(ctx, "cset x0, le");
                            break;
                        case TRACEY_BINARY_GE:
                            cg_emit(ctx, use_double ? "fcmp d0, d1" : "fcmp s0, s1");
                            cg_emit(ctx, "cset x0, ge");
                            break;
                        default:
                            break;
                    }

                    /* Arithmetic results stay in the FP register; move them
                     * back to x0. Comparison results are already in x0. */
                    if (e->as.binary_expr.op == TRACEY_BINARY_ADD ||
                        e->as.binary_expr.op == TRACEY_BINARY_SUB ||
                        e->as.binary_expr.op == TRACEY_BINARY_MUL ||
                        e->as.binary_expr.op == TRACEY_BINARY_DIV)
                    {
                        if (use_double)
                            cg_emit(ctx, "fmov x0, d0");
                        else
                            cg_emit(ctx, "fmov w0, s0");
                    }
                    return;
                }
            }

            switch (e->as.binary_expr.op)
            {
                case TRACEY_BINARY_ADD:
                    cg_emit(ctx, "add x0, x0, x1");
                    break;
                case TRACEY_BINARY_SUB:
                    cg_emit(ctx, "sub x0, x0, x1");
                    break;
                case TRACEY_BINARY_MUL:
                    cg_emit(ctx, "mul x0, x0, x1");
                    break;
                case TRACEY_BINARY_DIV:
                    cg_emit(ctx, "sdiv x0, x0, x1");
                    break;
                case TRACEY_BINARY_MOD:
                    cg_emit(ctx, "sdiv x9, x0, x1");
                    cg_emit(ctx, "msub x0, x9, x1, x0");
                    break;
                case TRACEY_BINARY_EQ:
                    cg_emit(ctx, "cmp x0, x1");
                    cg_emit(ctx, "cset x0, eq");
                    break;
                case TRACEY_BINARY_NE:
                    cg_emit(ctx, "cmp x0, x1");
                    cg_emit(ctx, "cset x0, ne");
                    break;
                case TRACEY_BINARY_LT:
                    cg_emit(ctx, "cmp x0, x1");
                    cg_emit(ctx, "cset x0, lt");
                    break;
                case TRACEY_BINARY_GT:
                    cg_emit(ctx, "cmp x0, x1");
                    cg_emit(ctx, "cset x0, gt");
                    break;
                case TRACEY_BINARY_LE:
                    cg_emit(ctx, "cmp x0, x1");
                    cg_emit(ctx, "cset x0, le");
                    break;
                case TRACEY_BINARY_GE:
                    cg_emit(ctx, "cmp x0, x1");
                    cg_emit(ctx, "cset x0, ge");
                    break;
                case TRACEY_BINARY_LOG_AND:
                    cg_emit(ctx, "cmp x0, #0");
                    cg_emit(ctx, "cset x0, ne");
                    cg_emit(ctx, "cmp x1, #0");
                    cg_emit(ctx, "cset x1, ne");
                    cg_emit(ctx, "and x0, x0, x1");
                    break;
                case TRACEY_BINARY_LOG_OR:
                    cg_emit(ctx, "cmp x0, #0");
                    cg_emit(ctx, "cset x0, ne");
                    cg_emit(ctx, "cmp x1, #0");
                    cg_emit(ctx, "cset x1, ne");
                    cg_emit(ctx, "orr x0, x0, x1");
                    break;
                case TRACEY_BINARY_BIT_AND:
                    cg_emit(ctx, "and x0, x0, x1");
                    break;
                case TRACEY_BINARY_BIT_OR:
                    cg_emit(ctx, "orr x0, x0, x1");
                    break;
                case TRACEY_BINARY_BIT_XOR:
                    cg_emit(ctx, "eor x0, x0, x1");
                    break;
                case TRACEY_BINARY_SHIFT_LEFT:
                case TRACEY_BINARY_SHIFT_RIGHT:
                    /* Shift instructions not yet supported by the assembler;
                     * pass through the left operand. */
                    break;
                default:
                    break;
            }
            return;
        }
        default:
            cg_emit(ctx, "mov x0, #0");
            return;
    }
}

static void cg_gen_stmt(struct tracey_codegen_ctx *ctx, const struct tracey_stmt *s,
                        const char *fn_name)
{
    size_t i;

    if (s == NULL)
        return;

    /* Debug line information follows the statement's source location. */
    if (s->loc.line > 0)
        cg_emitf(ctx, ".loc %zu", s->loc.line);

    switch (s->kind)
    {
        case TRACEY_STMT_BLOCK:
            for (i = 0; i < s->as.block_stmt.count; i++)
                cg_gen_stmt(ctx, s->as.block_stmt.statements[i], fn_name);
            return;
        case TRACEY_STMT_VAR_DECL:
            if (s->as.var_decl_stmt.init != NULL)
            {
                int o = cg_local_offset(ctx, s->as.var_decl_stmt.name);

                cg_gen_expr(ctx, s->as.var_decl_stmt.init);
                cg_emitf(ctx, "str x0, [x29, #%d]", o);
            }
            return;
        case TRACEY_STMT_EXPR:
            cg_gen_expr(ctx, s->as.expr_stmt.expression);
            return;
        case TRACEY_STMT_IF:
        {
            char *elseL = cg_new_label(ctx, ".Lelse");
            char *endL = cg_new_label(ctx, ".Lendif");

            cg_gen_expr(ctx, s->as.if_stmt.condition);
            cg_emit(ctx, "cmp x0, #0");
            cg_emitf(ctx, "beq %s", elseL);
            cg_gen_stmt(ctx, s->as.if_stmt.then_branch, fn_name);
            cg_emitf(ctx, "b %s", endL);
            cg_emitf(ctx, "%s:", elseL);
            if (s->as.if_stmt.else_branch != NULL)
                cg_gen_stmt(ctx, s->as.if_stmt.else_branch, fn_name);
            cg_emitf(ctx, "%s:", endL);
            free(elseL);
            free(endL);
            return;
        }
        case TRACEY_STMT_DO_WHILE:
        {
            char *startL = cg_new_label(ctx, ".Ldo");
            char *condL = cg_new_label(ctx, ".Ldocond");
            char *endL = cg_new_label(ctx, ".Lenddo");

            if (ctx->break_count == ctx->break_cap)
            {
                size_t cap = ctx->break_cap ? ctx->break_cap * 2 : 8;
                char **pp = (char **)realloc(ctx->break_stack, cap * sizeof(*pp));
                if (pp == NULL) { free(startL); free(condL); free(endL); return; }
                ctx->break_stack = pp;
                ctx->break_cap = cap;
            }
            ctx->break_stack[ctx->break_count++] = endL;
            if (ctx->continue_count == ctx->continue_cap)
            {
                size_t cap = ctx->continue_cap ? ctx->continue_cap * 2 : 8;
                char **pp = (char **)realloc(ctx->continue_stack, cap * sizeof(*pp));
                if (pp == NULL) { free(startL); free(condL); free(endL); ctx->break_count--; return; }
                ctx->continue_stack = pp;
                ctx->continue_cap = cap;
            }
            ctx->continue_stack[ctx->continue_count++] = condL;

            cg_emitf(ctx, "%s:", startL);
            cg_gen_stmt(ctx, s->as.do_while_stmt.body, fn_name);
            cg_emitf(ctx, "%s:", condL);
            cg_gen_expr(ctx, s->as.do_while_stmt.condition);
            cg_emit(ctx, "cmp x0, #0");
            cg_emitf(ctx, "b.ne %s", startL);
            cg_emitf(ctx, "%s:", endL);

            ctx->break_count--;
            ctx->continue_count--;
            free(startL);
            free(condL);
            free(endL);
            return;
        }
        case TRACEY_STMT_SWITCH:
        {
            char *endL = cg_new_label(ctx, ".Lendswitch");
            size_t _ci;

            if (ctx->break_count == ctx->break_cap)
            {
                size_t cap = ctx->break_cap ? ctx->break_cap * 2 : 8;
                char **pp = (char **)realloc(ctx->break_stack, cap * sizeof(*pp));
                if (pp == NULL) { free(endL); return; }
                ctx->break_stack = pp;
                ctx->break_cap = cap;
            }
            ctx->break_stack[ctx->break_count++] = endL;

            cg_gen_expr(ctx, s->as.switch_stmt.condition);
            cg_emit(ctx, "sub sp, sp, #16");
            cg_emit(ctx, "str x0, [sp]");

            for (_ci = 0; _ci < s->as.switch_stmt.case_count; _ci++)
            {
                char *nextL = cg_new_label(ctx, ".Lcase");
                cg_emit(ctx, "ldr x0, [sp]");
                cg_emitf(ctx, "mov x1, #%ld", s->as.switch_stmt.cases[_ci].value);
                cg_emit(ctx, "cmp x0, x1");
                cg_emitf(ctx, "b.ne %s", nextL);
                if (s->as.switch_stmt.cases[_ci].body)
                    cg_gen_stmt(ctx, s->as.switch_stmt.cases[_ci].body, fn_name);
                cg_emitf(ctx, "b %s", endL);
                cg_emitf(ctx, "%s:", nextL);
                free(nextL);
            }

            if (s->as.switch_stmt.default_body)
                cg_gen_stmt(ctx, s->as.switch_stmt.default_body, fn_name);

            cg_emitf(ctx, "%s:", endL);
            cg_emit(ctx, "add sp, sp, #16");
            ctx->break_count--;
            free(endL);
            return;
        }
        case TRACEY_STMT_WHILE:
        {
            char *startL = cg_new_label(ctx, ".Lwhile");
            char *endL = cg_new_label(ctx, ".Lendwhile");

            /* Push break/continue labels for nested loop support. */
            if (ctx->break_count == ctx->break_cap)
            {
                size_t cap = ctx->break_cap ? ctx->break_cap * 2 : 8;
                char **p = (char **)realloc(ctx->break_stack, cap * sizeof(*p));
                if (p == NULL) { free(startL); free(endL); return; }
                ctx->break_stack = p;
                ctx->break_cap = cap;
            }
            ctx->break_stack[ctx->break_count++] = endL;
            if (ctx->continue_count == ctx->continue_cap)
            {
                size_t cap = ctx->continue_cap ? ctx->continue_cap * 2 : 8;
                char **p = (char **)realloc(ctx->continue_stack, cap * sizeof(*p));
                if (p == NULL) { free(startL); free(endL); ctx->break_count--; return; }
                ctx->continue_stack = p;
                ctx->continue_cap = cap;
            }
            ctx->continue_stack[ctx->continue_count++] = startL;

            cg_emitf(ctx, "%s:", startL);
            cg_gen_expr(ctx, s->as.while_stmt.condition);
            cg_emit(ctx, "cmp x0, #0");
            cg_emitf(ctx, "beq %s", endL);
            cg_gen_stmt(ctx, s->as.while_stmt.body, fn_name);
            cg_emitf(ctx, "b %s", startL);
            cg_emitf(ctx, "%s:", endL);
            ctx->break_count--;
            ctx->continue_count--;
            free(startL);
            free(endL);
            return;
        }
        case TRACEY_STMT_FOR:
        {
            char *startL = cg_new_label(ctx, ".Lfor");
            char *endL = cg_new_label(ctx, ".Lendfor");
            char *incrL = cg_new_label(ctx, ".LforIncr");

            /* Push break/continue labels for nested loop support. */
            if (ctx->break_count == ctx->break_cap)
            {
                size_t cap = ctx->break_cap ? ctx->break_cap * 2 : 8;
                char **p = (char **)realloc(ctx->break_stack, cap * sizeof(*p));
                if (p == NULL) { free(startL); free(endL); free(incrL); return; }
                ctx->break_stack = p;
                ctx->break_cap = cap;
            }
            ctx->break_stack[ctx->break_count++] = endL;
            if (ctx->continue_count == ctx->continue_cap)
            {
                size_t cap = ctx->continue_cap ? ctx->continue_cap * 2 : 8;
                char **p = (char **)realloc(ctx->continue_stack, cap * sizeof(*p));
                if (p == NULL) { free(startL); free(endL); free(incrL); ctx->break_count--; return; }
                ctx->continue_stack = p;
                ctx->continue_cap = cap;
            }
            ctx->continue_stack[ctx->continue_count++] = incrL;

            if (s->as.for_stmt.init != NULL)
                cg_gen_stmt(ctx, s->as.for_stmt.init, fn_name);
            cg_emitf(ctx, "%s:", startL);
            if (s->as.for_stmt.condition != NULL)
            {
                cg_gen_expr(ctx, s->as.for_stmt.condition);
                cg_emit(ctx, "cmp x0, #0");
                cg_emitf(ctx, "beq %s", endL);
            }
            cg_gen_stmt(ctx, s->as.for_stmt.body, fn_name);
            cg_emitf(ctx, "%s:", incrL);
            if (s->as.for_stmt.update != NULL)
                cg_gen_expr(ctx, s->as.for_stmt.update);
            cg_emitf(ctx, "b %s", startL);
            cg_emitf(ctx, "%s:", endL);
            ctx->break_count--;
            ctx->continue_count--;
            free(startL);
            free(endL);
            free(incrL);
            return;
        }
        case TRACEY_STMT_BREAK:
        {
            if (ctx->break_count > 0)
                cg_emitf(ctx, "b %s", ctx->break_stack[ctx->break_count - 1]);
            return;
        }
        case TRACEY_STMT_CONTINUE:
        {
            if (ctx->continue_count > 0)
                cg_emitf(ctx, "b %s", ctx->continue_stack[ctx->continue_count - 1]);
            return;
        }
        case TRACEY_STMT_GOTO:
        {
            /* Forward-only goto: emit an unconditional branch to the label. */
            cg_emitf(ctx, "b %s", s->as.goto_stmt.label);
            return;
        }
        case TRACEY_STMT_LABEL:
        {
            /* Emit the label, then fall through to the labeled statement. */
            cg_emitf(ctx, "%s:", s->as.label_stmt.label);
            cg_gen_stmt(ctx, s->as.label_stmt.stmt, fn_name);
            return;
        }
        case TRACEY_STMT_RETURN:
            if (s->as.return_stmt.value != NULL)
                cg_gen_expr(ctx, s->as.return_stmt.value);
            cg_emitf(ctx, "b %s_ret", fn_name);
            return;
        default:
            return;
    }
}

/*
 * cg_emit_string_table - emit the module string table.
 *
 * Called once after all functions so that every literal referenced by any
 * function has a unique label. Escapes are written in assembler syntax.
 */
static void cg_emit_string_table(struct tracey_codegen_ctx *ctx)
{
    size_t i;

    if (ctx->strtab_count == 0)
        return;

    fprintf(ctx->out, "    .data\n");
    for (i = 0; i < ctx->strtab_count; i++)
    {
        const char *sv = ctx->strtab[i].value;
        size_t j;

        fprintf(ctx->out, "L%dstr%d:\n", ctx->module_id, ctx->strtab[i].index);
        fprintf(ctx->out, "    .asciz \"");
        for (j = 0; sv[j] != '\0'; j++)
        {
            unsigned char c = (unsigned char)sv[j];

            if (c == '\\')
                fprintf(ctx->out, "\\\\");
            else if (c == '"')
                fprintf(ctx->out, "\\\"");
            else if (c == '\n')
                fprintf(ctx->out, "\\n");
            else if (c == '\t')
                fprintf(ctx->out, "\\t");
            else if (c == '\r')
                fprintf(ctx->out, "\\r");
            else
                fputc(c, ctx->out);
        }
        fprintf(ctx->out, "\"\n");
    }
}

void tracey_codegen_generate(struct tracey_code_generator *cg, const struct tracey_program *program,
                             const char *source_name, FILE *out, int module_id,
                             bool emit_runtime_helpers)
{
    struct tracey_codegen_ctx ctx;
    size_t fi;

    if (cg == NULL || program == NULL || out == NULL)
        return;
    if (cg->arch != TRACEY_TARGET_AARCH64)
    {
        fprintf(out, "// codegen for this arch not implemented yet\n");
        return;
    }

    memset(&ctx, 0, sizeof(ctx));
    ctx.out = out;
    ctx.module_id = module_id;
    ctx.program = program;
    sb_init(&ctx.body);

    fprintf(out, "    .text\n");
    fprintf(out, "    .file \"%s\"\n", source_name ? source_name : "<input>");

    for (fi = 0; fi < program->function_count; fi++)
    {
        const struct tracey_function_decl *fn = program->functions[fi];
        int frame_size = 0;
        int cursor = 0;
        size_t i;

        if (fn == NULL || fn->body == NULL)
            continue;

        /* Locals are per function; the string table stays module-wide. */
        for (i = 0; i < ctx.local_count; i++)
            free(ctx.locals[i].name);
        free(ctx.locals);
        ctx.locals = NULL;
        ctx.local_count = 0;
        ctx.local_cap = 0;
        ctx.label_counter = 0;
        ctx.body.len = 0;
        if (ctx.body.data != NULL)
            ctx.body.data[0] = '\0';

        for (i = 0; i < fn->param_count; i++)
            cg_add_local(&ctx, fn->params[i].name, 8, false, fn->params[i].type, -1);
        cg_collect_vars_stmt(fn->body, &ctx);

        for (i = 0; i < ctx.local_count; i++)
        {
            cursor += ctx.locals[i].size;
            if (cursor % 8 != 0)
                cursor += 8 - (cursor % 8);
        }
        frame_size = cursor;
        if (frame_size % 16 != 0)
            frame_size += 16 - (frame_size % 16);

        cg_gen_stmt(&ctx, fn->body, fn->name);

        fprintf(out, "    .globl %s\n", fn->name);
        fprintf(out, "%s:\n", fn->name);
        fprintf(out, "    stp x29, x30, [sp, #-16]!\n");
        fprintf(out, "    mov x29, sp\n");
        if (frame_size > 0)
            fprintf(out, "    sub sp, sp, #%d\n", frame_size);
        for (i = 0; i < fn->param_count && i < 8; i++)
            fprintf(out, "    str x%zu, [x29, #%d]\n", i, cg_local_offset(&ctx, fn->params[i].name));
        if (fn->loc.line > 0)
            fprintf(out, "    .loc %zu\n", fn->loc.line);
        if (ctx.body.data != NULL)
            fputs(ctx.body.data, out);
        fprintf(out, "%s_ret:\n", fn->name);
        fprintf(out, "    mov sp, x29\n");
        fprintf(out, "    ldp x29, x30, [sp], #16\n");
        fprintf(out, "    ret\n");
    }

    if (emit_runtime_helpers)
        tracey_emit_runtime_helpers(out, cg->arch);

    cg_emit_string_table(&ctx);

    for (fi = 0; fi < ctx.local_count; fi++)
        free(ctx.locals[fi].name);
    free(ctx.locals);
    for (fi = 0; fi < ctx.strtab_count; fi++)
        free(ctx.strtab[fi].value);
    free(ctx.strtab);
    free(ctx.break_stack);
    free(ctx.continue_stack);
    sb_free(&ctx.body);
}
