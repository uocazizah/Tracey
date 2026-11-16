#include "tracey/codegen.h"
#include "codegen_internal.h"


void tracey_codegen_init(struct tracey_code_generator *cg, enum tracey_target_arch arch)
{
    if (cg == NULL)
        return;
    cg->arch = arch;
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
        {
            enum tracey_type_kind ptype = fn->params[i].type;
            int psz = 8;

            if (ptype == TRACEY_TYPE_STRUCT)
            {
                ptype = TRACEY_TYPE_STRUCT_PTR;
                psz = 8;
            }
            cg_add_local(&ctx, fn->params[i].name, psz, false, ptype,
                         fn->params[i].struct_id, fn->params[i].fn_sig);
        }
        {
            bool _rs = false;
            size_t _fi;
            if (ctx.program != NULL)
                for (_fi = 0; _fi < ctx.program->function_count; _fi++)
                    if (ctx.program->functions[_fi] != NULL &&
                        ctx.program->functions[_fi]->name != NULL &&
                        strcmp(ctx.program->functions[_fi]->name, fn->name) == 0 &&
                        ctx.program->functions[_fi]->return_type == TRACEY_TYPE_STRUCT)
                        _rs = true;
            if (_rs)
                cg_add_local(&ctx, "__ret", 8, false, TRACEY_TYPE_STRUCT_PTR,
                             fn->return_struct_id, NULL);
        }
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

        ctx.frame_size = frame_size;
        cg_gen_stmt(&ctx, fn->body, fn->name);

        fprintf(out, "    .globl %s\n", fn->name);
        fprintf(out, "%s:\n", fn->name);
        fprintf(out, "    stp x29, x30, [sp, #-16]!\n");
        fprintf(out, "    mov x29, sp\n");
        if (frame_size > 0)
            fprintf(out, "    sub sp, sp, #%d\n", frame_size);
        for (i = 0; i < fn->param_count && i < 8; i++)
            fprintf(out, "    str x%zu, [x29, #%d]\n", i, cg_local_offset(&ctx, fn->params[i].name));
        /*
         * Parameters beyond the first eight are passed on the stack by the
         * caller, in ascending order, starting at [x29, #16] (just above
         * the saved frame pointer/link register). Load each of them into
         * its local slot so the rest of codegen can treat them uniformly.
         */
        for (i = 8; i < fn->param_count; i++)
        {
            fprintf(out, "    ldr x9, [x29, #%d]\n", 16 + (int)((i - 8) * 8));
            fprintf(out, "    str x9, [x29, #%d]\n", cg_local_offset(&ctx, fn->params[i].name));
        }
        {
            bool _rs = false;
            size_t _fi;
            if (ctx.program != NULL)
                for (_fi = 0; _fi < ctx.program->function_count; _fi++)
                    if (ctx.program->functions[_fi] != NULL &&
                        ctx.program->functions[_fi]->name != NULL &&
                        strcmp(ctx.program->functions[_fi]->name, fn->name) == 0 &&
                        ctx.program->functions[_fi]->return_type == TRACEY_TYPE_STRUCT)
                        _rs = true;
            if (_rs)
                fprintf(out, "    str x8, [x29, #%d]\n", cg_local_offset(&ctx, "__ret"));
        }
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

