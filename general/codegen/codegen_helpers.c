#include "codegen_internal.h"


void sb_init(struct tracey_string_builder *sb)
{
    sb->data = NULL;
    sb->len = 0;
    sb->cap = 0;
}


void sb_free(struct tracey_string_builder *sb)
{
    free(sb->data);
    sb_init(sb);
}


void sb_reserve(struct tracey_string_builder *sb, size_t extra)
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


void sb_append(struct tracey_string_builder *sb, const char *s)
{
    size_t n = strlen(s ? s : "");

    sb_reserve(sb, n);
    memcpy(sb->data + sb->len, s ? s : "", n);
    sb->len += n;
    sb->data[sb->len] = '\0';
}


char *cg_strdup(const char *s)
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
int cg_get_str(struct tracey_codegen_ctx *ctx, const char *s)
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


int cg_find_local(struct tracey_codegen_ctx *ctx, const char *name)
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
int cg_local_offset(struct tracey_codegen_ctx *ctx, const char *name)
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


void cg_emit(struct tracey_codegen_ctx *ctx, const char *s)
{
    sb_append(&ctx->body, "    ");
    sb_append(&ctx->body, s);
    sb_append(&ctx->body, "\n");
}


void cg_emitf(struct tracey_codegen_ctx *ctx, const char *fmt, ...)
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
char *cg_new_label(struct tracey_codegen_ctx *ctx, const char *prefix)
{
    char buf[64];

    snprintf(buf, sizeof(buf), "%s%d", prefix, ctx->label_counter++);
    return cg_strdup(buf);
}


void cg_gen_expr(struct tracey_codegen_ctx *ctx, const struct tracey_expr *e);
void cg_gen_stmt(struct tracey_codegen_ctx *ctx, const struct tracey_stmt *s, const char *fn_name);

int cg_infer_struct_id(struct tracey_codegen_ctx *ctx, const struct tracey_expr *e);

/*
 * cg_infer_struct_id - resolve the struct id of an expression.
 *
 * Returns the struct definition index when e has struct value type, or -1
 * otherwise. Identifier results come from the local slot recorded during
 * variable collection; member results come from the member's own type in
 * the base struct. No diagnostics are produced.
 */
int cg_infer_struct_id(struct tracey_codegen_ctx *ctx, const struct tracey_expr *e)
{
    if (e == NULL)
        return -1;

    if (e->kind == TRACEY_EXPR_IDENT)
    {
        int idx = cg_find_local(ctx, e->as.ident_expr.name);

        if (idx >= 0 && (ctx->locals[idx].type == TRACEY_TYPE_STRUCT ||
                          ctx->locals[idx].type == TRACEY_TYPE_STRUCT_PTR))
            return ctx->locals[idx].struct_id;
        return -1;
    }

    if (e->kind == TRACEY_EXPR_CALL)
    {
        const char *cname = e->as.call_expr.callee ? e->as.call_expr.callee : "";
        size_t fi;

        if (ctx->program != NULL)
        {
            for (fi = 0; fi < ctx->program->function_count; fi++)
            {
                if (ctx->program->functions[fi] != NULL &&
                    ctx->program->functions[fi]->name != NULL &&
                    strcmp(ctx->program->functions[fi]->name, cname) == 0 &&
                    ctx->program->functions[fi]->return_type == TRACEY_TYPE_STRUCT)
                    return ctx->program->functions[fi]->return_struct_id;
            }
        }
        return -1;
    }

    if (e->kind == TRACEY_EXPR_DEREF)
    {
        if (e->as.deref_expr.operand != NULL &&
            e->as.deref_expr.operand->kind == TRACEY_EXPR_IDENT)
        {
            int idx = cg_find_local(ctx, e->as.deref_expr.operand->as.ident_expr.name);

            if (idx >= 0 && ctx->locals[idx].type == TRACEY_TYPE_STRUCT_PTR)
                return ctx->locals[idx].struct_id;
        }
        if (e->as.deref_expr.operand != NULL)
            return cg_infer_struct_id(ctx, e->as.deref_expr.operand);
        return -1;
    }

    if (e->kind == TRACEY_EXPR_ADDR)
    {
        if (e->as.addr_expr.operand != NULL &&
            e->as.addr_expr.operand->kind == TRACEY_EXPR_IDENT)
        {
            int idx = cg_find_local(ctx, e->as.addr_expr.operand->as.ident_expr.name);

            if (idx >= 0 && ctx->locals[idx].type == TRACEY_TYPE_STRUCT)
                return ctx->locals[idx].struct_id;
        }
        if (e->as.addr_expr.operand != NULL)
            return cg_infer_struct_id(ctx, e->as.addr_expr.operand);
        return -1;
    }

    if (e->kind == TRACEY_EXPR_INDEX)
    {
        int base_sid = cg_infer_struct_id(ctx, e->as.index_expr.base);

        if (base_sid < 0)
        {
            /* The base may be a struct array local. */
            if (e->as.index_expr.base->kind == TRACEY_EXPR_IDENT)
            {
                int idx = cg_find_local(ctx, e->as.index_expr.base->as.ident_expr.name);

                if (idx >= 0 && ctx->locals[idx].type == TRACEY_TYPE_STRUCT)
                    return ctx->locals[idx].struct_id;
            }
        }
        return base_sid;
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
const struct tracey_struct_member *
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


enum tracey_type_kind cg_infer_type(struct tracey_codegen_ctx *ctx,
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
            if (bt == TRACEY_TYPE_STRUCT)
                return TRACEY_TYPE_STRUCT;
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
            if (t == TRACEY_TYPE_STRUCT)
            {
                int sid = cg_infer_struct_id(ctx, e->as.addr_expr.operand);
                /* Store struct id in? cg_infer_type loses it; member access
                   resolves via cg_infer_struct_id separately. */
                (void)sid;
                return TRACEY_TYPE_STRUCT_PTR;
            }
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
            if (t == TRACEY_TYPE_SIZE_T_PTR)
                return TRACEY_TYPE_SIZE_T;
            if (t == TRACEY_TYPE_STRUCT_PTR)
                return TRACEY_TYPE_STRUCT;
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
        {
            const char *cname = e->as.call_expr.callee ? e->as.call_expr.callee : "";
            size_t fi;

            if (ctx->program != NULL)
            {
                for (fi = 0; fi < ctx->program->function_count; fi++)
                {
                    if (ctx->program->functions[fi] != NULL &&
                        ctx->program->functions[fi]->name != NULL &&
                        strcmp(ctx->program->functions[fi]->name, cname) == 0)
                    {
                        if (ctx->program->functions[fi]->return_type == TRACEY_TYPE_STRUCT)
                            return TRACEY_TYPE_STRUCT_PTR;
                        if (ctx->program->functions[fi]->return_type == TRACEY_TYPE_FUNC_PTR)
                            return TRACEY_TYPE_FUNC_PTR;
                    }
                }
            }
            return TRACEY_TYPE_INT;
        }
        case TRACEY_EXPR_TERNARY:
            return cg_infer_type(ctx, e->as.ternary_expr.then_branch);
        case TRACEY_EXPR_SIZEOF:
            return TRACEY_TYPE_INT;
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
void cg_add_local(struct tracey_codegen_ctx *ctx, const char *name, int size, bool is_array,
                         enum tracey_type_kind type, int struct_id, struct tracey_func_sig *fn_sig)
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
    ctx->locals[ctx->local_count].fn_sig = fn_sig;
    ctx->local_count++;
}


/*
 * cg_collect_vars_stmt - collect locals declared inside a statement tree.
 *
 * Arrays reserve eight bytes per element and decay to the matching pointer
 * type, which is what the expression generator expects when it computes the
 * address of an identifier.
 */
void cg_collect_vars_stmt(const struct tracey_stmt *s, struct tracey_codegen_ctx *ctx)
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
                size_t struct_sz = ctx->program->structs[sid].size;

                if (struct_sz <= 0)
                    struct_sz = 8;
                if (s->as.var_decl_stmt.array_size > 0)
                    sz = (int)(s->as.var_decl_stmt.array_size * struct_sz);
                else
                    sz = (int)struct_sz;
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
                         s->as.var_decl_stmt.array_size > 0, t, sid,
                         s->as.var_decl_stmt.fn_sig);
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
void cg_emit_load_immediate(struct tracey_codegen_ctx *ctx, uint64_t bits)
{
    cg_emitf(ctx, "mov x0, #%llu", (unsigned long long)(bits & 0xFFFFULL));
    cg_emitf(ctx, "movk x0, #%llu, lsl #16", (unsigned long long)((bits >> 16) & 0xFFFFULL));
    cg_emitf(ctx, "movk x0, #%llu, lsl #32", (unsigned long long)((bits >> 32) & 0xFFFFULL));
    cg_emitf(ctx, "movk x0, #%llu, lsl #48", (unsigned long long)((bits >> 48) & 0xFFFFULL));
}


/*
 * cg_type_size - return the byte size of a type for sizeof.
 */
size_t cg_type_size(struct tracey_codegen_ctx *ctx, enum tracey_type_kind type, int struct_id)
{
    switch (type)
    {
        case TRACEY_TYPE_CHAR:
        case TRACEY_TYPE_BOOL:
            return 1;
        case TRACEY_TYPE_SHORT:
        case TRACEY_TYPE_UNSIGNED_SHORT:
            return 2;
        case TRACEY_TYPE_INT:
        case TRACEY_TYPE_UNSIGNED_INT:
        case TRACEY_TYPE_FLOAT:
            return 4;
        case TRACEY_TYPE_STRUCT:
            if (ctx != NULL && ctx->program != NULL && struct_id >= 0 &&
                (size_t)struct_id < ctx->program->struct_count)
                return ctx->program->structs[struct_id].size ? ctx->program->structs[struct_id].size : 8;
            return 8;
        default:
            return 8;
    }
}


/*
 * cg_emit_string_table - emit the module string table.
 *
 * Called once after all functions so that every literal referenced by any
 * function has a unique label. Escapes are written in assembler syntax.
 */
void cg_emit_string_table(struct tracey_codegen_ctx *ctx)
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

