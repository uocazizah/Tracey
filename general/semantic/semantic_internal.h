#ifndef TRACEY_SEMANTIC_INTERNAL_H
#define TRACEY_SEMANTIC_INTERNAL_H

#include "tracey/semantic.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

char *tracey_strdup_local(const char *s);
void tracey_semantic_scope_free(struct tracey_semantic_scope *scope);
void tracey_semantic_string_array_free(char **items, size_t count);
void tracey_semantic_error(struct tracey_semantic_analyzer *sa, const char *msg, struct tracey_source_loc loc);
void tracey_semantic_push_scope(struct tracey_semantic_analyzer *sa);
void tracey_semantic_pop_scope(struct tracey_semantic_analyzer *sa);
bool tracey_semantic_declare(struct tracey_semantic_analyzer *sa, const char *name,
                            struct tracey_semantic_symbol sym, struct tracey_source_loc loc);
bool tracey_semantic_lookup(const struct tracey_semantic_analyzer *sa, const char *name,
                            struct tracey_semantic_symbol *out);
enum tracey_type_kind tracey_semantic_check_expr(struct tracey_semantic_analyzer *sa,
                                                struct tracey_expr *e,
                                                const struct tracey_function_decl *fn);
void tracey_semantic_check_stmt(struct tracey_semantic_analyzer *sa, struct tracey_stmt *s,
                               const struct tracey_function_decl *fn,
                               enum tracey_type_kind expected_return);
bool tracey_is_numeric(enum tracey_type_kind t);
bool tracey_is_integer(enum tracey_type_kind t);
bool tracey_is_pointer(enum tracey_type_kind t);
enum tracey_type_kind tracey_pointer_element_type(enum tracey_type_kind t);
bool tracey_types_assignable(enum tracey_type_kind dst, enum tracey_type_kind src);
int tracey_semantic_expr_struct_id(struct tracey_semantic_analyzer *sa, const struct tracey_expr *e);
const struct tracey_struct_member *tracey_semantic_member(struct tracey_semantic_analyzer *sa, int base_sid, const char *field);
void tracey_semantic_check_function(struct tracey_semantic_analyzer *sa, struct tracey_function_decl *fn);

#endif /* TRACEY_SEMANTIC_INTERNAL_H */
