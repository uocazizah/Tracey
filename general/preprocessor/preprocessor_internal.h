#ifndef TRACEY_PREPROCESSOR_INTERNAL_H
#define TRACEY_PREPROCESSOR_INTERNAL_H

#include "tracey/preprocessor.h"
#include "tracey/common.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct tracey_macro
{
    char *name;
    char *replacement;
    char **params;
    size_t param_count;
    bool function_like;
    bool variadic;
    bool expanding;
};

struct tracey_macro_table
{
    struct tracey_macro *items;
    size_t count;
    size_t cap;
};

struct tracey_if_entry
{
    bool active;      /* current block is being emitted */
    bool parent_on;   /* parent context was active */
    bool saw_true;    /* some #if in this chain was true */
};

struct tracey_if_stack
{
    struct tracey_if_entry *items;
    size_t count;
    size_t cap;
};

struct tracey_visited_set
{
    char **items;
    size_t count;
    size_t cap;
};

/* Macro table. */
void tracey_macro_table_init(struct tracey_macro_table *table);
void tracey_macro_table_free(struct tracey_macro_table *table);
int tracey_macro_find(const struct tracey_macro_table *table, const char *name);
void tracey_macro_undef(struct tracey_macro_table *table, const char *name);
bool tracey_macro_define(struct tracey_macro_table *table, const char *name,
                        char *replacement, char **params, size_t param_count,
                        bool function_like, bool variadic);

/* Conditional inclusion stack. */
void tracey_if_stack_init(struct tracey_if_stack *stack);
void tracey_if_stack_free(struct tracey_if_stack *stack);
bool tracey_if_stack_active(const struct tracey_if_stack *stack);
void tracey_if_push(struct tracey_if_stack *stack, bool condition, bool parent_active);
void tracey_if_pop(struct tracey_if_stack *stack);

/* Visited set (for #include guards). */
void tracey_visited_init(struct tracey_visited_set *set);
void tracey_visited_free(struct tracey_visited_set *set);
bool tracey_visited_contains(const struct tracey_visited_set *set, const char *path);
void tracey_visited_add(struct tracey_visited_set *set, const char *path);

/* String and path helpers. */
char *tracey_strdup_range(const char *start, size_t len);
char *tracey_join_path(const char *dir, const char *name);
bool tracey_file_exists(const char *path);
char *tracey_read_file_all(const char *path);
char *tracey_dirname_dup(const char *path);
bool tracey_is_id_start(char c);
bool tracey_is_id_cont(char c);
bool tracey_is_space(char c);
char *tracey_trim_dup(const char *s);
void tracey_append_buf(char **buf, size_t *len, size_t *cap, const char *s, size_t n);

/* Macro expansion. */
char **tracey_split_macro_args(const char *text, size_t open_paren,
                              size_t *end_pos, size_t *argc_out);
char *tracey_instantiate_function_macro(struct tracey_macro *macro,
                                       char **args, size_t argc,
                                       struct tracey_macro_table *table,
                                       int *depth);
char *tracey_expand_macros(const char *text, struct tracey_macro_table *table, int *depth);
char *tracey_replace_defined(const char *text, struct tracey_macro_table *table);

/* Directive evaluation. */
bool tracey_is_defined_builtin(const char *name);
bool tracey_eval_defined(const char *name, struct tracey_macro_table *table);
bool tracey_eval_simple_cond(const char *s);

/* Directive handling. */
bool tracey_handle_include(const char *line, size_t line_len, const char *base_dir,
                           const char *const *include_paths, size_t include_path_count,
                           struct tracey_visited_set *visited,
                           struct tracey_macro_table *table,
                           struct tracey_if_stack *if_stack,
                           struct tracey_string *out, bool *had_error);
bool tracey_handle_define(const char *line, size_t line_len,
                          struct tracey_macro_table *table);
bool tracey_handle_undef(const char *line, size_t line_len,
                         struct tracey_macro_table *table);
bool tracey_handle_error_directive(const char *line, size_t line_len, bool warning,
                                   bool *had_error);
bool tracey_handle_ifdef(const char *line, size_t line_len,
                         struct tracey_if_stack *stack,
                         struct tracey_macro_table *table,
                         bool parent_active, bool ifndef_mode);
bool tracey_handle_if(const char *line, size_t line_len,
                      struct tracey_if_stack *stack,
                      struct tracey_macro_table *table,
                      bool parent_active);
bool tracey_handle_else(const char *line, size_t line_len,
                        struct tracey_if_stack *stack);
bool tracey_handle_elif(const char *line, size_t line_len,
                        struct tracey_if_stack *stack,
                        struct tracey_macro_table *table);
bool tracey_handle_endif(const char *line, size_t line_len,
                         struct tracey_if_stack *stack);
bool tracey_handle_defined_directive(const char *line, size_t line_len,
                                    struct tracey_macro_table *table);

/* Recursive preprocessing. */
bool tracey_preprocess_recursive(const char *content, size_t content_len,
                                 const char *base_dir,
                                 const char *const *include_paths, size_t include_path_count,
                                 struct tracey_visited_set *visited,
                                 struct tracey_macro_table *table,
                                 struct tracey_if_stack *if_stack,
                                 struct tracey_string *out, bool *had_error);

#endif /* TRACEY_PREPROCESSOR_INTERNAL_H */
