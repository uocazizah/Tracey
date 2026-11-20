#ifndef TRACEY_LEXER_INTERNAL_H
#define TRACEY_LEXER_INTERNAL_H

#include "tracey/lexer.h"

#include "tracey/config.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*
 * Low-level lexer cursor helpers.
 *
 * These operate directly on the lexer state and are shared between the
 * core lexer implementation and the scanning routines. They do not
 * allocate memory and do not change ownership of the source buffer.
 */
bool tracey_lexer_at_end(const struct tracey_lexer *lexer);
char tracey_lexer_current(const struct tracey_lexer *lexer);
char tracey_lexer_current_next(const struct tracey_lexer *lexer);
char tracey_lexer_advance(struct tracey_lexer *lexer);
bool tracey_lexer_match(struct tracey_lexer *lexer, char expected);

/*
 * tracey_scan_token - scan and return the next lexical token.
 *
 * Skips whitespace and comments, then dispatches to the appropriate
 * scanner for the current character. Advances the lexer position as a
 * side effect and returns a token whose lexeme points into the source
 * buffer owned by the lexer.
 */
struct tracey_token tracey_scan_token(struct tracey_lexer *lexer);

#endif /* TRACEY_LEXER_INTERNAL_H */
