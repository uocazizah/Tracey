#ifndef TRACEY_SEMANTIC_H
#define TRACEY_SEMANTIC_H

#include <stdbool.h>
#include <stddef.h>

#include "tracey/ast.h"

#ifdef __cplusplus
extern "C" {
#endif

struct tracey_semantic_error
{
    char *message;
    size_t line;
    size_t column;
};

struct tracey_semantic_symbol
{
    enum tracey_type_kind type;
    bool is_function;
    int arity;
};

struct tracey_semantic_binding
{
    char *name;
    struct tracey_semantic_symbol symbol;
};

struct tracey_semantic_scope
{
    struct tracey_semantic_binding *bindings;
    size_t count;
    size_t cap;
};

struct tracey_semantic_analyzer
{
    bool had_error;
    struct tracey_semantic_error *errors;
    size_t error_count;
    size_t error_cap;
    struct tracey_semantic_scope *scopes;
    size_t scope_count;
    size_t scope_cap;
};

void tracey_semantic_init(struct tracey_semantic_analyzer *sa);
void tracey_semantic_free(struct tracey_semantic_analyzer *sa);
bool tracey_semantic_analyze(struct tracey_semantic_analyzer *sa, struct tracey_program *program);

#ifdef __cplusplus
}
#endif

#endif /* TRACEY_SEMANTIC_H */
