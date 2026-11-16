#include "codegen_internal.h"


void cg_gen_stmt(struct tracey_codegen_ctx *ctx, const struct tracey_stmt *s,
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
            if (s->as.var_decl_stmt.init != NULL &&
                s->as.var_decl_stmt.init->kind == TRACEY_EXPR_INIT_LIST)
            {
                struct tracey_init_list_expr *il = &s->as.var_decl_stmt.init->as.init_list_expr;
                int base = cg_local_offset(ctx, s->as.var_decl_stmt.name);
                int sid = s->as.var_decl_stmt.struct_id;
                size_t i;

                if (sid >= 0 && ctx->program != NULL && (size_t)sid < ctx->program->struct_count)
                {
                    for (i = 0; i < il->count; i++)
                    {
                        size_t off = 0;

                        if (i < ctx->program->structs[sid].member_count)
                            off = ctx->program->structs[sid].members[i].offset;
                        cg_gen_expr(ctx, il->items[i]);
                        cg_emitf(ctx, "str x0, [x29, #%d]", base + (int)off);
                    }
                }
            }
            else if (s->as.var_decl_stmt.init != NULL)
            {
                int o = cg_local_offset(ctx, s->as.var_decl_stmt.name);
                int is_struct = (s->as.var_decl_stmt.type == TRACEY_TYPE_STRUCT);

                if (is_struct && (cg_infer_type(ctx, s->as.var_decl_stmt.init) == TRACEY_TYPE_STRUCT ||
                                  cg_infer_type(ctx, s->as.var_decl_stmt.init) == TRACEY_TYPE_STRUCT_PTR))
                {
                    size_t sz = 8;
                    int sid = s->as.var_decl_stmt.struct_id;
                    if (sid >= 0 && ctx->program != NULL && (size_t)sid < ctx->program->struct_count)
                        sz = ctx->program->structs[sid].size;
                    if (sz == 0) sz = 8;

                    cg_gen_expr(ctx, s->as.var_decl_stmt.init);
                    cg_emit(ctx, "sub sp, sp, #16");
                    cg_emit(ctx, "str x0, [sp]");
                    if (o < 0)
                        cg_emitf(ctx, "sub x1, x29, #%d", -o);
                    else
                        cg_emitf(ctx, "add x1, x29, #%d", o);
                    cg_emit(ctx, "ldr x0, [sp]");
                    cg_emit(ctx, "add sp, sp, #16");
                    cg_emitf(ctx, "mov x2, #%zu", sz);
                    cg_emit(ctx, "cbz x2, .Lcopydonev");
                    {
                        char *lbl = cg_new_label(ctx, ".Lcopyloopv");
                        cg_emitf(ctx, "%s:", lbl);
                        cg_emit(ctx, "ldrb w3, [x0]");
                        cg_emit(ctx, "strb w3, [x1]");
                        cg_emit(ctx, "add x0, x0, #1");
                        cg_emit(ctx, "add x1, x1, #1");
                        cg_emit(ctx, "sub x2, x2, #1");
                        cg_emitf(ctx, "cbnz x2, %s", lbl);
                        free(lbl);
                    }
                    cg_emit(ctx, ".Lcopydonev:");
                }
                else
                {
                    cg_gen_expr(ctx, s->as.var_decl_stmt.init);
                    cg_emitf(ctx, "str x0, [x29, #%d]", o);
                }
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
        {
            bool returns_struct = false;
            size_t fi;
            if (ctx->program != NULL && fn_name != NULL)
                for (fi = 0; fi < ctx->program->function_count; fi++)
                    if (ctx->program->functions[fi] != NULL &&
                        ctx->program->functions[fi]->name != NULL &&
                        strcmp(ctx->program->functions[fi]->name, fn_name) == 0 &&
                        ctx->program->functions[fi]->return_type == TRACEY_TYPE_STRUCT)
                        returns_struct = true;
            if (s->as.return_stmt.value != NULL)
            {
                size_t _sz = 8;
                int _sid = -1;
                size_t _fi;
                if (ctx->program != NULL && fn_name != NULL)
                    for (_fi = 0; _fi < ctx->program->function_count; _fi++)
                        if (ctx->program->functions[_fi] != NULL &&
                            ctx->program->functions[_fi]->name != NULL &&
                            strcmp(ctx->program->functions[_fi]->name, fn_name) == 0)
                        { _sid = ctx->program->functions[_fi]->return_struct_id; break; }
                if (_sid >= 0 && ctx->program != NULL && (size_t)_sid < ctx->program->struct_count)
                    _sz = ctx->program->structs[_sid].size;
                if (_sz == 0) _sz = 8;

                if (returns_struct && cg_infer_type(ctx, s->as.return_stmt.value) == TRACEY_TYPE_STRUCT)
                    cg_gen_addr(ctx, s->as.return_stmt.value);
                else
                    cg_gen_expr(ctx, s->as.return_stmt.value);
                if (returns_struct)
                {
                    /* x0 = source address; copy into *(__ret). */
                    cg_emit(ctx, "sub sp, sp, #16");
                    cg_emit(ctx, "str x0, [sp]");
                    cg_emitf(ctx, "ldr x1, [x29, #%d]", cg_local_offset(ctx, "__ret"));
                    cg_emit(ctx, "ldr x0, [sp]");
                    cg_emit(ctx, "add sp, sp, #16");
                    cg_emitf(ctx, "mov x2, #%zu", _sz);
                    cg_emit(ctx, "cbz x2, .Lretcopydone");
                    {
                        char *lbl = cg_new_label(ctx, ".Lretcopyloop");
                        cg_emitf(ctx, "%s:", lbl);
                        cg_emit(ctx, "ldrb w3, [x0]");
                        cg_emit(ctx, "strb w3, [x1]");
                        cg_emit(ctx, "add x0, x0, #1");
                        cg_emit(ctx, "add x1, x1, #1");
                        cg_emit(ctx, "sub x2, x2, #1");
                        cg_emitf(ctx, "cbnz x2, %s", lbl);
                        free(lbl);
                    }
                    cg_emit(ctx, ".Lretcopydone:");
                }
            }
            cg_emitf(ctx, "b %s_ret", fn_name);
            return;
        }
        default:
            return;
    }
}

