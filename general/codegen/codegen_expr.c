#include "codegen_internal.h"


/*
 * cg_gen_printf - lower a printf call with a literal format string.
 *
 * Text segments are interned and printed with the string helper. %d and %s
 * consume one call argument each; %% emits a literal percent sign. A format
 * string that is not a literal is ignored, matching the original generator.
 */
void cg_gen_printf(struct tracey_codegen_ctx *ctx, const struct tracey_call_expr *call)
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
void cg_gen_addr(struct tracey_codegen_ctx *ctx, const struct tracey_expr *e)
{
    if (e == NULL)
        return;

    switch (e->kind)
    {
        case TRACEY_EXPR_IDENT:
        {
            int idx = cg_find_local(ctx, e->as.ident_expr.name);
            int o = cg_local_offset(ctx, e->as.ident_expr.name);

            if (idx >= 0 && ctx->locals[idx].type == TRACEY_TYPE_STRUCT_PTR)
            {
                cg_emitf(ctx, "ldr x0, [x29, #%d]", o);
                return;
            }
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
            int stride = 8;

            if (bt == TRACEY_TYPE_CHAR_PTR)
                stride = 1;
            else if (bt == TRACEY_TYPE_STRUCT)
            {
                int sid = -1;

                if (e->as.index_expr.base->kind == TRACEY_EXPR_IDENT)
                {
                    int li = cg_find_local(ctx, e->as.index_expr.base->as.ident_expr.name);

                    if (li >= 0)
                        sid = ctx->locals[li].struct_id;
                }
                else
                    sid = cg_infer_struct_id(ctx, e->as.index_expr.base);

                if (sid >= 0 && ctx->program != NULL && (size_t)sid < ctx->program->struct_count)
                    stride = (int)ctx->program->structs[sid].size;
                if (stride <= 0)
                    stride = 8;
            }

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


void cg_gen_expr(struct tracey_codegen_ctx *ctx, const struct tracey_expr *e)
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
            bool is_func = false;
            size_t fi;

            if (li < 0 && ctx->program != NULL)
            {
                for (fi = 0; fi < ctx->program->function_count; fi++)
                {
                    if (ctx->program->functions[fi] != NULL &&
                        ctx->program->functions[fi]->name != NULL &&
                        strcmp(ctx->program->functions[fi]->name, e->as.ident_expr.name) == 0)
                    {
                        is_func = true;
                        break;
                    }
                }
            }

            if (is_func)
            {
                cg_emitf(ctx, "adr x0, %s", e->as.ident_expr.name);
                return;
            }

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
            else if (e->as.unary_expr.op == TRACEY_UNARY_BIT_NOT)
            {
                cg_emit(ctx, "mvn x0, x0");
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
                    else if (op->kind == TRACEY_EXPR_MEMBER)
                    {
                        cg_gen_addr(ctx, op);
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

            /*
             * General call lowering for the AArch64 calling convention.
             *
             * Arguments are first evaluated into a temporary stack buffer so
             * that no argument evaluation clobbers a previously computed
             * value. The first eight arguments are then loaded into x0..x7.
             * Any argument beyond the eighth is left on the stack in
             * ascending order (arg8 at [sp], arg9 at [sp, #8], ...) at the
             * point of the call, as required by the ABI. The caller cleans
             * those stack arguments up afterwards.
             *
             * When the callee returns a struct, the hidden result pointer
             * (x8) addresses a buffer allocated just above the argument
             * buffer; it is left allocated so the caller can read the result.
             */
            {
                bool _ret_struct = false;
                size_t _rsz = 8;
                size_t _fi;
                size_t _n = e->as.call_expr.arg_count;
                size_t _buf_size = (_n > 0) ? ((_n * 8 + 15) & ~((size_t)15)) : 0;
                size_t _asz = 0;
                size_t _nreg;

                if (ctx->program != NULL)
                    for (_fi = 0; _fi < ctx->program->function_count; _fi++)
                        if (ctx->program->functions[_fi] != NULL &&
                            ctx->program->functions[_fi]->name != NULL &&
                            strcmp(ctx->program->functions[_fi]->name, callee) == 0 &&
                            ctx->program->functions[_fi]->return_type == TRACEY_TYPE_STRUCT)
                        {
                            _ret_struct = true;
                            _rsz = ctx->program->functions[_fi]->return_struct_id >= 0 &&
                                   (size_t)ctx->program->functions[_fi]->return_struct_id < ctx->program->struct_count
                                       ? ctx->program->structs[ctx->program->functions[_fi]->return_struct_id].size
                                       : 8;
                            if (_rsz == 0) _rsz = 8;
                            break;
                        }
                if (_ret_struct)
                {
                    _asz = (_rsz + 15) & ~((size_t)15);
                    if (_asz == 0) _asz = 16;
                }

                if (_buf_size + _asz > 0)
                    cg_emitf(ctx, "sub sp, sp, #%zu", _buf_size + _asz);

                for (i = 0; i < _n; i++)
                {
                    if (cg_infer_type(ctx, e->as.call_expr.args[i]) == TRACEY_TYPE_STRUCT)
                        cg_gen_addr(ctx, e->as.call_expr.args[i]);
                    else
                        cg_gen_expr(ctx, e->as.call_expr.args[i]);
                    cg_emitf(ctx, "str x0, [sp, #%zu]", i * 8);
                }

                _nreg = _n < 8 ? _n : 8;
                for (i = 0; i < _nreg; i++)
                    cg_emitf(ctx, "ldr x%zu, [sp, #%zu]", i, i * 8);

                if (_ret_struct)
                {
                    cg_emitf(ctx, "add x8, sp, #%zu", _buf_size);
                }

                if (_n > 8)
                {
                    cg_emit(ctx, "add sp, sp, #64");
                }

                {
                    int _fpi = cg_find_local(ctx, callee);

                    if (_fpi >= 0 && ctx->locals[_fpi].type == TRACEY_TYPE_FUNC_PTR)
                    {
                        int _o = cg_local_offset(ctx, callee);

                        cg_emitf(ctx, "ldr x9, [x29, #%d]", _o);
                        cg_emit(ctx, "blr x9");
                    }
                    else
                    {
                        cg_emitf(ctx, "bl %s", callee);
                    }
                }

                {
                    size_t _add = (_ret_struct ? _buf_size : _buf_size + _asz) -
                                  (_n > 8 ? 64 : 0);
                    if (_add > 0)
                        cg_emitf(ctx, "add sp, sp, #%zu", _add);
                }
                if (_ret_struct)
                    cg_emit(ctx, "mov x0, sp");
            }
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
        {
            int base_sid = cg_infer_struct_id(ctx, e->as.member_expr.base);
            const struct tracey_struct_member *m =
                cg_member(ctx, base_sid, e->as.member_expr.field);
            enum tracey_type_kind mt = (m != NULL) ? m->type : TRACEY_TYPE_INT;

            cg_gen_addr(ctx, e);
            if (mt == TRACEY_TYPE_CHAR || mt == TRACEY_TYPE_BOOL)
                cg_emit(ctx, "ldrb w0, [x0]");
            else if (mt == TRACEY_TYPE_SHORT || mt == TRACEY_TYPE_UNSIGNED_SHORT)
                cg_emit(ctx, "ldrh w0, [x0]");
            else if (mt == TRACEY_TYPE_INT || mt == TRACEY_TYPE_UNSIGNED_INT ||
                     mt == TRACEY_TYPE_FLOAT)
                cg_emit(ctx, "ldr w0, [x0]");
            else
                cg_emit(ctx, "ldr x0, [x0]");
            return;
        }
        case TRACEY_EXPR_BINARY:
        {
            if (e->as.binary_expr.op == TRACEY_BINARY_ASSIGN)
            {
                if (e->as.binary_expr.left->kind == TRACEY_EXPR_IDENT)
                {
                    int o = cg_local_offset(ctx, e->as.binary_expr.left->as.ident_expr.name);
                    int li = cg_find_local(ctx, e->as.binary_expr.left->as.ident_expr.name);
                    int is_struct = (li >= 0 && ctx->locals[li].type == TRACEY_TYPE_STRUCT);

                    if (is_struct && (cg_infer_type(ctx, e->as.binary_expr.right) == TRACEY_TYPE_STRUCT ||
                                      cg_infer_type(ctx, e->as.binary_expr.right) == TRACEY_TYPE_STRUCT_PTR))
                    {
                        size_t sz = 8;
                        int sid = (li >= 0) ? ctx->locals[li].struct_id : -1;

                        if (sid >= 0 && ctx->program != NULL && (size_t)sid < ctx->program->struct_count)
                            sz = ctx->program->structs[sid].size;
                        if (sz == 0)
                            sz = 8;

                        cg_gen_expr(ctx, e->as.binary_expr.right);
                        cg_emit(ctx, "sub sp, sp, #16");
                        cg_emit(ctx, "str x0, [sp]");
                        if (o < 0)
                            cg_emitf(ctx, "sub x1, x29, #%d", -o);
                        else
                            cg_emitf(ctx, "add x1, x29, #%d", o);
                        cg_emit(ctx, "ldr x0, [sp]");
                        cg_emit(ctx, "add sp, sp, #16");
                        cg_emitf(ctx, "mov x2, #%zu", sz);
                        cg_emit(ctx, "cbz x2, .Lcopydone");
                        {
                            char *lbl = cg_new_label(ctx, ".Lcopyloop");
                            cg_emitf(ctx, "%s:", lbl);
                            cg_emit(ctx, "ldrb w3, [x0]");
                            cg_emit(ctx, "strb w3, [x1]");
                            cg_emit(ctx, "add x0, x0, #1");
                            cg_emit(ctx, "add x1, x1, #1");
                            cg_emit(ctx, "sub x2, x2, #1");
                            cg_emitf(ctx, "cbnz x2, %s", lbl);
                            free(lbl);
                        }
                        cg_emit(ctx, ".Lcopydone:");
                        return;
                    }

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
                    int base_sid = cg_infer_struct_id(ctx, e->as.binary_expr.left->as.member_expr.base);
                    const struct tracey_struct_member *m =
                        cg_member(ctx, base_sid, e->as.binary_expr.left->as.member_expr.field);
                    enum tracey_type_kind mt = (m != NULL) ? m->type : TRACEY_TYPE_INT;

                    cg_gen_addr(ctx, e->as.binary_expr.left);
                    cg_emit(ctx, "sub sp, sp, #16");
                    cg_emit(ctx, "str x0, [sp]");
                    cg_gen_expr(ctx, e->as.binary_expr.right);
                    cg_emit(ctx, "ldr x1, [sp]");
                    cg_emit(ctx, "add sp, sp, #16");
                    if (mt == TRACEY_TYPE_CHAR || mt == TRACEY_TYPE_BOOL)
                        cg_emit(ctx, "strb w0, [x1]");
                    else if (mt == TRACEY_TYPE_SHORT || mt == TRACEY_TYPE_UNSIGNED_SHORT)
                        cg_emit(ctx, "strh w0, [x1]");
                    else if (mt == TRACEY_TYPE_INT || mt == TRACEY_TYPE_UNSIGNED_INT ||
                             mt == TRACEY_TYPE_FLOAT)
                        cg_emit(ctx, "str w0, [x1]");
                    else
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
                if (e->as.binary_expr.left->kind == TRACEY_EXPR_MEMBER)
                {
                    struct tracey_expr *lhs = e->as.binary_expr.left;
                    int base_sid = cg_infer_struct_id(ctx, lhs->as.member_expr.base);
                    const struct tracey_struct_member *m = cg_member(ctx, base_sid, lhs->as.member_expr.field);
                    enum tracey_type_kind mt = (m != NULL) ? m->type : TRACEY_TYPE_INT;

                    cg_gen_expr(ctx, lhs);
                    cg_emit(ctx, "sub sp, sp, #16");
                    cg_emit(ctx, "str x0, [sp]");
                    cg_gen_addr(ctx, lhs);
                    cg_emit(ctx, "sub sp, sp, #16");
                    cg_emit(ctx, "str x0, [sp]");
                    cg_gen_expr(ctx, e->as.binary_expr.right);
                    cg_emit(ctx, "mov x1, x0");
                    cg_emit(ctx, "ldr x0, [sp, #16]");
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
                        case TRACEY_BINARY_SHL_ASSIGN:
                            if (e->as.binary_expr.right != NULL &&
                                e->as.binary_expr.right->kind == TRACEY_EXPR_INT)
                            {
                                long long _sa = e->as.binary_expr.right->as.int_expr.value;
                                if (_sa >= 0 && _sa < 64)
                                    cg_emitf(ctx, "lsl x0, x0, #%lld", _sa);
                                else
                                    cg_emit(ctx, "lsl x0, x0, x1");
                            }
                            else
                                cg_emit(ctx, "lsl x0, x0, x1");
                            break;
                        case TRACEY_BINARY_SHR_ASSIGN:
                            if (e->as.binary_expr.right != NULL &&
                                e->as.binary_expr.right->kind == TRACEY_EXPR_INT)
                            {
                                long long _sa = e->as.binary_expr.right->as.int_expr.value;
                                if (_sa >= 0 && _sa < 64)
                                    cg_emitf(ctx, "lsr x0, x0, #%lld", _sa);
                                else
                                    cg_emit(ctx, "lsr x0, x0, x1");
                            }
                            else
                                cg_emit(ctx, "lsr x0, x0, x1");
                            break;
                        case TRACEY_BINARY_AND_ASSIGN: cg_emit(ctx, "and x0, x0, x1"); break;
                        case TRACEY_BINARY_OR_ASSIGN: cg_emit(ctx, "orr x0, x0, x1"); break;
                        case TRACEY_BINARY_XOR_ASSIGN: cg_emit(ctx, "eor x0, x0, x1"); break;
                        default: break;
                    }
                    cg_emit(ctx, "ldr x2, [sp]");
                    cg_emit(ctx, "add sp, sp, #32");
                    if (mt == TRACEY_TYPE_CHAR || mt == TRACEY_TYPE_BOOL)
                        cg_emit(ctx, "strb w0, [x2]");
                    else if (mt == TRACEY_TYPE_SHORT || mt == TRACEY_TYPE_UNSIGNED_SHORT)
                        cg_emit(ctx, "strh w0, [x2]");
                    else if (mt == TRACEY_TYPE_INT || mt == TRACEY_TYPE_UNSIGNED_INT || mt == TRACEY_TYPE_FLOAT)
                        cg_emit(ctx, "str w0, [x2]");
                    else
                        cg_emit(ctx, "str x0, [x2]");
                    return;
                }
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
                        case TRACEY_BINARY_SHL_ASSIGN:
                            if (e->as.binary_expr.right != NULL &&
                                e->as.binary_expr.right->kind == TRACEY_EXPR_INT)
                            {
                                long long _sa = e->as.binary_expr.right->as.int_expr.value;
                                if (_sa >= 0 && _sa < 64)
                                    cg_emitf(ctx, "lsl x0, x0, #%lld", _sa);
                                else
                                    cg_emit(ctx, "lsl x0, x0, x1");
                            }
                            else
                                cg_emit(ctx, "lsl x0, x0, x1");
                            break;
                        case TRACEY_BINARY_SHR_ASSIGN:
                            if (e->as.binary_expr.right != NULL &&
                                e->as.binary_expr.right->kind == TRACEY_EXPR_INT)
                            {
                                long long _sa = e->as.binary_expr.right->as.int_expr.value;
                                if (_sa >= 0 && _sa < 64)
                                    cg_emitf(ctx, "lsr x0, x0, #%lld", _sa);
                                else
                                    cg_emit(ctx, "lsr x0, x0, x1");
                            }
                            else
                                cg_emit(ctx, "lsr x0, x0, x1");
                            break;
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

            /*
             * Shift by a compile-time constant can use the immediate form
             * of the shift instruction. Detect an integer literal shift
             * amount here and emit `lsl/lsr x0, x0, #n` directly, avoiding
             * evaluation of the (constant) right operand. Non-constant or
             * out-of-range amounts fall through to the register form.
             */
            if ((e->as.binary_expr.op == TRACEY_BINARY_SHIFT_LEFT ||
                 e->as.binary_expr.op == TRACEY_BINARY_SHIFT_RIGHT) &&
                e->as.binary_expr.right != NULL &&
                e->as.binary_expr.right->kind == TRACEY_EXPR_INT)
            {
                long long shift_amount = e->as.binary_expr.right->as.int_expr.value;

                if (shift_amount >= 0 && shift_amount < 64)
                {
                    cg_gen_expr(ctx, e->as.binary_expr.left);
                    if (e->as.binary_expr.op == TRACEY_BINARY_SHIFT_LEFT)
                        cg_emitf(ctx, "lsl x0, x0, #%lld", shift_amount);
                    else
                        cg_emitf(ctx, "lsr x0, x0, #%lld", shift_amount);
                    return;
                }
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
                    cg_emit(ctx, "lsl x0, x0, x1");
                    break;
                case TRACEY_BINARY_SHIFT_RIGHT:
                    cg_emit(ctx, "lsr x0, x0, x1");
                    break;
                default:
                    break;
            }
            return;
        }
        case TRACEY_EXPR_SIZEOF:
        {
            size_t sz = 8;
            if (e->as.sizeof_expr.is_type)
                sz = cg_type_size(ctx, e->as.sizeof_expr.type, e->as.sizeof_expr.struct_id);
            else if (e->as.sizeof_expr.operand != NULL)
            {
                enum tracey_type_kind t = cg_infer_type(ctx, e->as.sizeof_expr.operand);
                int sid = -1;
                if (t == TRACEY_TYPE_STRUCT)
                    sid = cg_infer_struct_id(ctx, e->as.sizeof_expr.operand);
                sz = cg_type_size(ctx, t, sid);
            }
            cg_emit_load_immediate(ctx, sz);
            return;
        }
        default:
            cg_emit(ctx, "mov x0, #0");
            return;
    }
}

