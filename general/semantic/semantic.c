#include "semantic_internal.h"


void tracey_semantic_init(struct tracey_semantic_analyzer *sa)
{
    if (sa == NULL)
        return;
    memset(sa, 0, sizeof(*sa));
}


void tracey_semantic_free(struct tracey_semantic_analyzer *sa)
{
    size_t i;
    if (sa == NULL)
        return;
    for (i = 0; i < sa->error_count; i++)
        free(sa->errors[i].message);
    free(sa->errors);
    sa->errors = NULL;
    sa->error_count = 0;
    sa->error_cap = 0;
    for (i = 0; i < sa->warning_count; i++)
        free(sa->warnings[i].message);
    free(sa->warnings);
    sa->warnings = NULL;
    sa->warning_count = 0;
    sa->warning_cap = 0;
    for (i = 0; i < sa->scope_count; i++)
        tracey_semantic_scope_free(&sa->scopes[i]);
    free(sa->scopes);
    tracey_semantic_string_array_free(sa->goto_labels, sa->goto_label_count);
    free(sa->goto_label_lines);
    sa->goto_label_lines = NULL;
    tracey_semantic_string_array_free(sa->declared_labels, sa->declared_label_count);
    free(sa->declared_label_lines);
    sa->declared_label_lines = NULL;
    tracey_semantic_init(sa);
}


bool tracey_semantic_analyze(struct tracey_semantic_analyzer *sa, struct tracey_program *program)
{
    size_t i;
    if (sa == NULL || program == NULL)
        return false;
    tracey_semantic_free(sa);
    tracey_semantic_init(sa);
    sa->program = program;
    tracey_semantic_push_scope(sa);

    for (i = 0; i < program->function_count; i++)
    {
        struct tracey_function_decl *fn = program->functions[i];
        struct tracey_semantic_symbol sym;
        sym.type = fn->return_type;
        sym.is_function = true;
        sym.arity = (int)fn->param_count;
        sym.struct_id = fn->return_struct_id;
        sym.fn_sig = fn->return_fn_sig;
        tracey_semantic_declare(sa, fn->name, sym, fn->loc);
    }
    for (i = 0; i < program->function_count; i++)
        tracey_semantic_check_function(sa, program->functions[i]);

    tracey_semantic_pop_scope(sa);
    return !sa->had_error;
}

