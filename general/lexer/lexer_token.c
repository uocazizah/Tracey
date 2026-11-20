#include "tracey/lexer.h"

#include "lexer_internal.h"

#include "tracey/config.h"

#include <stdio.h>
#include <string.h>

bool tracey_token_is_error(const struct tracey_token *token)
{
    return token != NULL && token->type == TRACEY_TOKEN_ERROR;
}

const char *tracey_token_type_string(enum tracey_token_type type)
{
    switch (type)
    {
        case TRACEY_TOKEN_EOF:
            return "EOF";
        case TRACEY_TOKEN_IDENTIFIER:
            return "IDENTIFIER";
        case TRACEY_TOKEN_INTEGER:
            return "INTEGER";
        case TRACEY_TOKEN_FLOATING:
            return "FLOATING";
        case TRACEY_TOKEN_STRING:
            return "STRING";
        case TRACEY_TOKEN_KEYWORD_INT:
            return "KEYWORD_INT";
        case TRACEY_TOKEN_KEYWORD_RETURN:
            return "KEYWORD_RETURN";
        case TRACEY_TOKEN_KEYWORD_VOID:
            return "KEYWORD_VOID";
        case TRACEY_TOKEN_KEYWORD_BOOL:
            return "KEYWORD_BOOL";
        case TRACEY_TOKEN_KEYWORD_CHAR:
            return "KEYWORD_CHAR";
        case TRACEY_TOKEN_KEYWORD_FLOAT:
            return "KEYWORD_FLOAT";
        case TRACEY_TOKEN_KEYWORD_DOUBLE:
            return "KEYWORD_DOUBLE";
        case TRACEY_TOKEN_KEYWORD_TRUE:
            return "KEYWORD_TRUE";
        case TRACEY_TOKEN_KEYWORD_FALSE:
            return "KEYWORD_FALSE";
        case TRACEY_TOKEN_KEYWORD_IF:
            return "KEYWORD_IF";
        case TRACEY_TOKEN_KEYWORD_ELSE:
            return "KEYWORD_ELSE";
        case TRACEY_TOKEN_KEYWORD_WHILE:
            return "KEYWORD_WHILE";
        case TRACEY_TOKEN_KEYWORD_FOR:
            return "KEYWORD_FOR";
        case TRACEY_TOKEN_KEYWORD_BREAK:
            return "KEYWORD_BREAK";
        case TRACEY_TOKEN_KEYWORD_CONTINUE:
            return "KEYWORD_CONTINUE";
        case TRACEY_TOKEN_KEYWORD_GOTO:
            return "KEYWORD_GOTO";
        case TRACEY_TOKEN_KEYWORD_LONG:
            return "KEYWORD_LONG";
        case TRACEY_TOKEN_KEYWORD_SHORT:
            return "KEYWORD_SHORT";
        case TRACEY_TOKEN_KEYWORD_UNSIGNED:
            return "KEYWORD_UNSIGNED";
        case TRACEY_TOKEN_KEYWORD_SIGNED:
            return "KEYWORD_SIGNED";
        case TRACEY_TOKEN_KEYWORD_SIZE_T:
            return "KEYWORD_SIZE_T";
        case TRACEY_TOKEN_LPAREN:
            return "LPAREN";
        case TRACEY_TOKEN_RPAREN:
            return "RPAREN";
        case TRACEY_TOKEN_LBRACE:
            return "LBRACE";
        case TRACEY_TOKEN_RBRACE:
            return "RBRACE";
        case TRACEY_TOKEN_LBRACKET:
            return "LBRACKET";
        case TRACEY_TOKEN_RBRACKET:
            return "RBRACKET";
        case TRACEY_TOKEN_SEMICOLON:
            return "SEMICOLON";
        case TRACEY_TOKEN_COMMA:
            return "COMMA";
        case TRACEY_TOKEN_DOT:
            return "DOT";
        case TRACEY_TOKEN_ASSIGN:
            return "ASSIGN";
        case TRACEY_TOKEN_PLUS:
            return "PLUS";
        case TRACEY_TOKEN_MINUS:
            return "MINUS";
        case TRACEY_TOKEN_STAR:
            return "STAR";
        case TRACEY_TOKEN_SLASH:
            return "SLASH";
        case TRACEY_TOKEN_PERCENT:
            return "PERCENT";
        case TRACEY_TOKEN_BANG:
            return "BANG";
        case TRACEY_TOKEN_LESS:
            return "LESS";
        case TRACEY_TOKEN_GREATER:
            return "GREATER";
        case TRACEY_TOKEN_LESS_EQUAL:
            return "LESS_EQUAL";
        case TRACEY_TOKEN_GREATER_EQUAL:
            return "GREATER_EQUAL";
        case TRACEY_TOKEN_EQUAL_EQUAL:
            return "EQUAL_EQUAL";
        case TRACEY_TOKEN_BANG_EQUAL:
            return "BANG_EQUAL";
        case TRACEY_TOKEN_AMP_AMP:
            return "AMP_AMP";
        case TRACEY_TOKEN_AMPERSAND:
            return "AMPERSAND";
        case TRACEY_TOKEN_PIPE_PIPE:
            return "PIPE_PIPE";
        case TRACEY_TOKEN_PIPE:
            return "PIPE";
        case TRACEY_TOKEN_CARET:
            return "CARET";
        case TRACEY_TOKEN_TILDE:
            return "TILDE";
        case TRACEY_TOKEN_LESS_LESS:
            return "LESS_LESS";
        case TRACEY_TOKEN_GREATER_GREATER:
            return "GREATER_GREATER";
        case TRACEY_TOKEN_PLUS_EQUAL:
            return "PLUS_EQUAL";
        case TRACEY_TOKEN_MINUS_EQUAL:
            return "MINUS_EQUAL";
        case TRACEY_TOKEN_STAR_EQUAL:
            return "STAR_EQUAL";
        case TRACEY_TOKEN_SLASH_EQUAL:
            return "SLASH_EQUAL";
        case TRACEY_TOKEN_PERCENT_EQUAL:
            return "PERCENT_EQUAL";
        case TRACEY_TOKEN_LESS_LESS_EQUAL:
            return "LESS_LESS_EQUAL";
        case TRACEY_TOKEN_GREATER_GREATER_EQUAL:
            return "GREATER_GREATER_EQUAL";
        case TRACEY_TOKEN_AMPERSAND_EQUAL:
            return "AMPERSAND_EQUAL";
        case TRACEY_TOKEN_PIPE_EQUAL:
            return "PIPE_EQUAL";
        case TRACEY_TOKEN_CARET_EQUAL:
            return "CARET_EQUAL";
        case TRACEY_TOKEN_QUESTION:
            return "QUESTION";
        case TRACEY_TOKEN_COLON:
            return "COLON";
        case TRACEY_TOKEN_CHAR:
            return "CHAR";
        case TRACEY_TOKEN_DEFINED:
            return "DEFINED";
        case TRACEY_TOKEN_ERROR:
            return "ERROR";
        default:
            return "UNKNOWN";
    }
}

const char *tracey_token_error_string(enum tracey_token_error error)
{
    switch (error)
    {
        case TRACEY_TOKEN_ERROR_NONE:
            return "No error";
        case TRACEY_TOKEN_ERROR_INVALID_CHARACTER:
            return "Invalid character in source";
        case TRACEY_TOKEN_ERROR_INVALID_NUMBER_SUFFIX:
            return "Invalid numeric literal suffix";
        case TRACEY_TOKEN_ERROR_INVALID_ESCAPE:
            return "Invalid escape sequence in string literal";
        case TRACEY_TOKEN_ERROR_INVALID_CHAR_ESCAPE:
            return "Invalid escape sequence in character literal";
        case TRACEY_TOKEN_ERROR_UNTERMINATED_STRING:
            return "Unterminated string literal";
        case TRACEY_TOKEN_ERROR_UNTERMINATED_CHAR:
            return "Unterminated character literal";
        case TRACEY_TOKEN_ERROR_UNTERMINATED_COMMENT:
            return "Unterminated block comment";
        default:
            return "Unknown lexer error";
    }
}

void tracey_token_print_debug(const struct tracey_token *token, FILE *out)
{
    if (token == NULL || out == NULL)
        return;
    fprintf(out, "Token(type=%s, line=%zu, column=%zu, lexeme=\"",
            tracey_token_type_string(token->type), token->line, token->column);
    if (token->lexeme != NULL)
        fwrite(token->lexeme, 1, token->lexeme_len, out);
    fprintf(out, "\")\n");
}

void tracey_lexer_print_debug(const struct tracey_lexer *lexer, FILE *out)
{
    if (lexer == NULL || out == NULL)
        return;
    fprintf(out, "Lexer(position=%zu, line=%zu, column=%zu, has_peeked=%s)\n",
            lexer->position, lexer->line, lexer->column,
            lexer->has_peeked ? "true" : "false");
}

void tracey_token_print_error(const struct tracey_token *token, const char *path, FILE *out)
{
    size_t i;

    if (token == NULL || out == NULL)
        return;
    if (path == NULL)
        path = "<input>";

    fprintf(out, "Error: %s:%zu:%zu: %s", path, token->line, token->column,
            tracey_token_error_string(token->error));

    if (token->lexeme != NULL && token->lexeme_len > 0)
    {
        fprintf(out, " near '");
        for (i = 0; i < token->lexeme_len; i++)
        {
            unsigned char c = (unsigned char)token->lexeme[i];
            if (c == '\n')
                fprintf(out, "\\n");
            else if (c == '\t')
                fprintf(out, "\\t");
            else if (c == '\r')
                fprintf(out, "\\r");
            else if (c >= 0x20 && c <= 0x7e)
                fputc(c, out);
            else
                fprintf(out, "\\x%02X", c);
        }
        fprintf(out, "'");
    }

    fprintf(out, "\n");
}
