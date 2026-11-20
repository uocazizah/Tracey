#include "tracey/lexer.h"

#include "lexer_internal.h"

/*
 * tracey_lexer_init - initialize a lexer over a source buffer.
 *
 * content may be NULL only when content_len is zero. The buffer is not
 * copied and must outlive the lexer.
 */
void tracey_lexer_init(struct tracey_lexer *lexer, const char *content, size_t content_len)
{
    if (lexer == NULL)
        return;
    lexer->content = content;
    lexer->content_len = content_len;
    lexer->position = 0;
    lexer->line = 1;
    lexer->column = 1;
    lexer->has_peeked = false;
    memset(&lexer->peeked, 0, sizeof(lexer->peeked));
}

bool tracey_lexer_at_end(const struct tracey_lexer *lexer)
{
    if (lexer == NULL)
        return true;
    return lexer->position >= lexer->content_len;
}

bool tracey_lexer_is_at_end(const struct tracey_lexer *lexer)
{
    if (lexer == NULL)
        return true;
    return tracey_lexer_at_end(lexer);
}

char tracey_lexer_current(const struct tracey_lexer *lexer)
{
    if (tracey_lexer_at_end(lexer))
        return '\0';
    return lexer->content[lexer->position];
}

char tracey_lexer_current_next(const struct tracey_lexer *lexer)
{
    if (lexer->position + 1 >= lexer->content_len)
        return '\0';
    return lexer->content[lexer->position + 1];
}

char tracey_lexer_advance(struct tracey_lexer *lexer)
{
    char c;

    if (tracey_lexer_at_end(lexer))
        return '\0';

    c = lexer->content[lexer->position];
    lexer->position++;

    if (c == '\n')
    {
        lexer->line++;
        lexer->column = 1;
    }
    else
    {
        lexer->column++;
    }

    return c;
}

bool tracey_lexer_match(struct tracey_lexer *lexer, char expected)
{
    if (tracey_lexer_at_end(lexer))
        return false;
    if (lexer->content[lexer->position] != expected)
        return false;
    tracey_lexer_advance(lexer);
    return true;
}

struct tracey_token tracey_lexer_next(struct tracey_lexer *lexer)
{
    struct tracey_token tok;

    if (lexer == NULL)
    {
        memset(&tok, 0, sizeof(tok));
        return tok;
    }

    if (lexer->has_peeked)
    {
        tok = lexer->peeked;
        lexer->has_peeked = false;
        return tok;
    }

    return tracey_scan_token(lexer);
}

struct tracey_token tracey_lexer_peek(struct tracey_lexer *lexer)
{
    if (lexer == NULL)
    {
        struct tracey_token tok;
        memset(&tok, 0, sizeof(tok));
        return tok;
    }

    if (!lexer->has_peeked)
    {
        lexer->peeked = tracey_scan_token(lexer);
        lexer->has_peeked = true;
    }

    return lexer->peeked;
}
