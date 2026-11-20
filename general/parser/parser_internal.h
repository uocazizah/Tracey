#ifndef TRACEY_PARSER_INTERNAL_H
#define TRACEY_PARSER_INTERNAL_H

#include "tracey/parser.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

bool tracey_block_push(struct tracey_block_stmt *block, struct tracey_stmt *stmt);
bool tracey_is_assignable(const struct tracey_expr *e);
bool tracey_parse_function(struct tracey_parser *parser, struct tracey_program *program);
bool tracey_parse_int_literal(const struct tracey_token *tok, unsigned long long *out);
enum tracey_type_kind tracey_int_literal_type(const struct tracey_token *tok);
bool tracey_parse_struct_definition(struct tracey_parser *parser);
bool tracey_parse_typedef(struct tracey_parser *parser);
bool tracey_parser_add_typedef(struct tracey_parser *parser, const char *name,
                                         enum tracey_type_kind type,
                                         struct tracey_func_sig *fn_sig);
bool tracey_parser_check(const struct tracey_parser *parser, enum tracey_token_type type);
bool tracey_parser_expect(struct tracey_parser *parser, enum tracey_token_type type,
                                 const char *msg);
bool tracey_parser_is_typedef_alias(const struct tracey_parser *parser);
bool tracey_parser_match(struct tracey_parser *parser, enum tracey_token_type type);
bool tracey_parser_parse(struct tracey_parser *parser, struct tracey_program *out);
bool tracey_program_push_function(struct tracey_program *program, struct tracey_function_decl *fn);
char *tracey_strdup_n(const char *s, size_t n);
char *tracey_token_strdup(const struct tracey_token *tok);
enum tracey_type_kind tracey_parse_type(struct tracey_parser *parser);
enum tracey_type_kind tracey_parse_type_ex(struct tracey_parser *parser,
                                                  int *out_struct_id,
                                                  struct tracey_func_sig **out_fn_sig);
struct tracey_expr *tracey_make_binary(struct tracey_expr *left, enum tracey_binary_op op,
                                              struct tracey_expr *right);
struct tracey_expr *tracey_parse_additive(struct tracey_parser *parser);
struct tracey_expr *tracey_parse_assignment(struct tracey_parser *parser);
struct tracey_expr *tracey_parse_bitwise_and(struct tracey_parser *parser);
struct tracey_expr *tracey_parse_bitwise_or(struct tracey_parser *parser);
struct tracey_expr *tracey_parse_bitwise_xor(struct tracey_parser *parser);
struct tracey_expr *tracey_parse_equality(struct tracey_parser *parser);
struct tracey_expr *tracey_parse_expression(struct tracey_parser *parser);
struct tracey_expr *tracey_parse_logical_and(struct tracey_parser *parser);
struct tracey_expr *tracey_parse_logical_or(struct tracey_parser *parser);
struct tracey_expr *tracey_parse_multiplicative(struct tracey_parser *parser);
struct tracey_expr *tracey_parse_primary(struct tracey_parser *parser);
struct tracey_expr *tracey_parse_relational(struct tracey_parser *parser);
struct tracey_expr *tracey_parse_shift(struct tracey_parser *parser);
struct tracey_expr *tracey_parse_ternary(struct tracey_parser *parser);
struct tracey_expr *tracey_parse_unary(struct tracey_parser *parser);
struct tracey_stmt *tracey_parse_block(struct tracey_parser *parser);
struct tracey_stmt *tracey_parse_break(struct tracey_parser *parser);
struct tracey_stmt *tracey_parse_continue(struct tracey_parser *parser);
struct tracey_stmt *tracey_parse_do(struct tracey_parser *parser);
struct tracey_stmt *tracey_parse_for(struct tracey_parser *parser);
struct tracey_stmt *tracey_parse_goto(struct tracey_parser *parser);
struct tracey_stmt *tracey_parse_if(struct tracey_parser *parser);
struct tracey_stmt *tracey_parse_return(struct tracey_parser *parser);
struct tracey_stmt *tracey_parse_statement(struct tracey_parser *parser);
struct tracey_stmt *tracey_parse_switch(struct tracey_parser *parser);
struct tracey_stmt *tracey_parse_var_decl_as_stmt(struct tracey_parser *parser);
struct tracey_stmt *tracey_parse_while(struct tracey_parser *parser);
void tracey_parser_advance(struct tracey_parser *parser);
void tracey_parser_error(struct tracey_parser *parser, const char *msg, const struct tracey_token *tok);
void tracey_parser_free(struct tracey_parser *parser);
void tracey_parser_init(struct tracey_parser *parser, struct tracey_lexer *lexer);
void tracey_parser_synchronize(struct tracey_parser *parser);

#endif /* TRACEY_PARSER_INTERNAL_H */
