#include <tracey/lexer.h>
#include "lexer_internal.h"
#include <stdio.h>

void tracey_lexer_print_debug(const tracey_lexer_t* lexer, FILE* out)
{
    if (!out) out = stdout;
    if (!lexer) {
        fprintf(out, "Lexer: NULL\n");
        return;
    }

    fprintf(out, "Lexer:\n");
    fprintf(out, "  Position: %zu / %zu\n", lexer->position, lexer->length);
    fprintf(out, "  Line: %zu\n", lexer->line);
    fprintf(out, "  Column: %zu\n", lexer->column);
    fprintf(out, "  Has peeked: %s\n", lexer->has_peeked ? "true" : "false");

    if (lexer->has_peeked) {
        fprintf(out, "  Peeked token:\n");
        tracey_token_print_debug(&lexer->peeked_token, out);
    }

    /* Print remaining content preview */
    if (lexer->position < lexer->length) {
        size_t remaining = lexer->length - lexer->position;
        size_t preview_len = remaining < 80 ? remaining : 80;
        fprintf(out, "  Remaining content preview:\n");
        fprintf(out, "  \"");
        for (size_t i = 0; i < preview_len; i++) {
            char c = lexer->content[lexer->position + i];
            if (c == '\n') fprintf(out, "\\n");
            else if (c == '\t') fprintf(out, "\\t");
            else if (c == '\r') fprintf(out, "\\r");
            else fprintf(out, "%c", c);
        }
        if (remaining > 80) fprintf(out, "...");
        fprintf(out, "\"\n");
    }
}

void tracey_token_print_debug(const tracey_token_t* token, FILE* out)
{
    if (!out) out = stdout;
    if (!token) {
        fprintf(out, "Token: NULL\n");
        return;
    }

    fprintf(out, "Token:\n");
    fprintf(out, "  Type: %s\n", tracey_token_type_str(token->type));
    fprintf(out, "  Line: %zu\n", token->line);
    fprintf(out, "  Column: %zu\n", token->column);
    fprintf(out, "  Length: %zu\n", token->length);
    if (token->start && token->length > 0) {
        fprintf(out, "  Text: ");
        fwrite(token->start, 1, token->length, out);
        fprintf(out, "\n");
    }
}