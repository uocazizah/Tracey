#include "preprocessor_internal.h"

/* ------------------------------------------------------------------ */
/* Macro expansion                                                    */
/* ------------------------------------------------------------------ */

char *tracey_trim_dup(const char *s)
{
    size_t start = 0;
    size_t end;
    char *out;

    if (s == NULL)
        return strdup("");
    end = strlen(s);
    while (start < end && tracey_is_space(s[start]))
        start++;
    while (end > start && tracey_is_space(s[end - 1]))
        end--;
    out = tracey_strdup_range(s + start, end - start);
    if (out == NULL)
        return strdup("");
    return out;
}

void tracey_append_buf(char **buf, size_t *len, size_t *cap, const char *s,
                              size_t n)
{
    if (n == 0)
        return;
    if (*len + n + 1 > *cap)
    {
        size_t new_cap = *cap ? *cap : 64;
        while (*len + n + 1 > new_cap)
            new_cap *= 2;
        char *tmp = (char *)realloc(*buf, new_cap);
        if (tmp == NULL)
            return;
        *buf = tmp;
        *cap = new_cap;
    }
    memcpy(*buf + *len, s, n);
    *len += n;
    (*buf)[*len] = '\0';
}

char **tracey_split_macro_args(const char *text, size_t open_paren,
                                     size_t *end_pos, size_t *argc_out)
{
    size_t len = strlen(text);
    size_t i = open_paren + 1;
    int depth = 1;
    size_t cap = 8;
    size_t count = 0;
    char **args = (char **)malloc(cap * sizeof(char *));
    size_t seg_start = i;

    if (args == NULL)
    {
        *end_pos = open_paren;
        *argc_out = 0;
        return NULL;
    }

    while (i < len && depth > 0)
    {
        if (text[i] == '(')
            depth++;
        else if (text[i] == ')')
        {
            depth--;
            if (depth == 0)
            {
                char *seg = tracey_strdup_range(text + seg_start, i - seg_start);
                if (count == cap)
                {
                    cap *= 2;
                    char **tmp = (char **)realloc(args, cap * sizeof(char *));
                    if (tmp == NULL)
                    {
                        free(seg);
                        break;
                    }
                    args = tmp;
                }
                args[count++] = seg ? seg : strdup("");
                i++;
                break;
            }
        }
        else if (text[i] == ',' && depth == 1)
        {
            char *seg = tracey_strdup_range(text + seg_start, i - seg_start);
            if (count == cap)
            {
                cap *= 2;
                char **tmp = (char **)realloc(args, cap * sizeof(char *));
                if (tmp == NULL)
                {
                    free(seg);
                    break;
                }
                args = tmp;
            }
            args[count++] = seg ? seg : strdup("");
            seg_start = i + 1;
        }
        i++;
    }

    if (depth > 0)
    {
        /* Unterminated: treat rest as one argument. */
        char *seg = tracey_strdup_range(text + seg_start, len - seg_start);
        if (count == cap)
        {
            cap *= 2;
            char **tmp = (char **)realloc(args, cap * sizeof(char *));
            if (tmp != NULL)
                args = tmp;
        }
        if (args != NULL)
            args[count++] = seg ? seg : strdup("");
        i = len;
    }

    *end_pos = i;
    *argc_out = count;
    return args;
}

char *tracey_instantiate_function_macro(struct tracey_macro *macro,
                                              char **args, size_t argc,
                                              struct tracey_macro_table *table,
                                              int *depth)
{
    size_t i;
    size_t param_count = macro->param_count;
    char **argvals = NULL;
    char *out = NULL;
    size_t out_len = 0;
    size_t out_cap = 64;
    bool paste_pending = false;

    out = (char *)malloc(out_cap);
    if (out == NULL)
        return NULL;
    out[0] = '\0';

    /* Build per-parameter argument value (trimmed). */
    argvals = (char **)calloc(param_count ? param_count : 1, sizeof(char *));
    if (argvals == NULL)
    {
        free(out);
        return NULL;
    }

    if (macro->variadic && param_count > 0)
    {
        size_t variadic_index = param_count - 1;
        if (argc > param_count)
        {
            /* Join extra arguments with commas. */
            size_t total = 0;
            size_t j;
            for (j = variadic_index; j < argc; j++)
                total += strlen(args[j]) + 2;
            char *joined = (char *)malloc(total + 1);
            size_t pos = 0;
            if (joined != NULL)
            {
                joined[0] = '\0';
                for (j = variadic_index; j < argc; j++)
                {
                    char *targ = tracey_trim_dup(args[j]);
                    if (j > variadic_index)
                    {
                        joined[pos++] = ',';
                        joined[pos++] = ' ';
                        joined[pos] = '\0';
                    }
                    size_t alen = strlen(targ);
                    memcpy(joined + pos, targ, alen);
                    pos += alen;
                    joined[pos] = '\0';
                    free(targ);
                }
            }
            for (i = 0; i < variadic_index && i < argc; i++)
                argvals[i] = tracey_trim_dup(i < argc ? args[i] : "");
            argvals[variadic_index] = joined ? tracey_trim_dup(joined) : strdup("");
            free(joined);
            for (i = variadic_index + 1; i < param_count; i++)
                argvals[i] = strdup("");
        }
        else
        {
            for (i = 0; i < param_count; i++)
            {
                if (i < argc)
                    argvals[i] = tracey_trim_dup(args[i]);
                else if (i == variadic_index)
                    argvals[i] = strdup("");
                else
                    argvals[i] = strdup("");
            }
        }
    }
    else
    {
        for (i = 0; i < param_count; i++)
        {
            if (i < argc)
                argvals[i] = tracey_trim_dup(args[i]);
            else
                argvals[i] = strdup("");
        }
    }

    /* Helper to find param index by name. */
    /* Scan replacement. */
    {
        char *repl = macro->replacement ? macro->replacement : "";
        size_t rlen = strlen(repl);
        size_t k = 0;
        while (k < rlen)
        {
            char c = repl[k];

            if (c == '#')
            {
                /* ## pasting operator? */
                if (k + 1 < rlen && repl[k + 1] == '#')
                {
                    /* Remove trailing spaces from output. */
                    while (out_len > 0 && tracey_is_space(out[out_len - 1]))
                        out_len--;
                    out[out_len] = '\0';
                    paste_pending = true;
                    k += 2;
                    while (k < rlen && tracey_is_space(repl[k]))
                        k++;
                    continue;
                }

                /* Stringification: # param */
                {
                    size_t j = k + 1;
                    while (j < rlen && tracey_is_space(repl[j]))
                        j++;
                    if (j < rlen && tracey_is_id_start(repl[j]))
                    {
                        size_t start = j;
                        while (j < rlen && tracey_is_id_cont(repl[j]))
                            j++;
                        size_t nlen = j - start;
                        char *pname = tracey_strdup_range(repl + start, nlen);
                        int pidx = -1;
                        for (i = 0; i < param_count; i++)
                        {
                            if (macro->params[i] != NULL &&
                                strcmp(macro->params[i], pname) == 0)
                            {
                                pidx = (int)i;
                                break;
                            }
                        }
                        if (pidx >= 0)
                        {
                            /* Append stringified argument. */
                            char *arg = argvals[pidx];
                            tracey_append_buf(&out, &out_len, &out_cap, "\"", 1);
                            for (size_t t = 0; arg != NULL && arg[t] != '\0'; t++)
                            {
                                if (arg[t] == '\\' || arg[t] == '"')
                                {
                                    char esc[2] = { '\\', arg[t] };
                                    tracey_append_buf(&out, &out_len, &out_cap, esc, 2);
                                }
                                else if (arg[t] == '\n')
                                {
                                    tracey_append_buf(&out, &out_len, &out_cap, "\\n", 2);
                                }
                                else
                                {
                                    tracey_append_buf(&out, &out_len, &out_cap, &arg[t], 1);
                                }
                            }
                            tracey_append_buf(&out, &out_len, &out_cap, "\"", 1);
                            free(pname);
                            k = j;
                            /* If paste pending, clear. */
                            paste_pending = false;
                            continue;
                        }
                        free(pname);
                    }
                }

                /* Not stringification: copy '#'. */
                tracey_append_buf(&out, &out_len, &out_cap, "#", 1);
                k++;
                paste_pending = false;
                continue;
            }

            if (tracey_is_space(c))
            {
                if (paste_pending)
                {
                    /* skip spaces in paste context */
                    k++;
                    continue;
                }
                tracey_append_buf(&out, &out_len, &out_cap, " ", 1);
                k++;
                continue;
            }

            if (tracey_is_id_start(c))
            {
                size_t start = k;
                while (k < rlen && tracey_is_id_cont(repl[k]))
                    k++;
                size_t nlen = k - start;
                char *name = tracey_strdup_range(repl + start, nlen);

                /* Find param index. */
                int pidx = -1;
                for (i = 0; i < param_count; i++)
                {
                    if (macro->params[i] != NULL &&
                        strcmp(macro->params[i], name) == 0)
                    {
                        pidx = (int)i;
                        break;
                    }
                }

                if (pidx >= 0)
                {
                    char *arg = argvals[pidx];
                    if (arg == NULL)
                        arg = "";
                    /* If paste pending, trim trailing spaces already removed. */
                    tracey_append_buf(&out, &out_len, &out_cap, arg, strlen(arg));
                }
                else
                {
                    tracey_append_buf(&out, &out_len, &out_cap, name, nlen);
                }

                free(name);
                if (paste_pending)
                    paste_pending = false;
                continue;
            }

            /* Regular character. */
            if (paste_pending)
            {
                /* Remove any trailing spaces already done; just append. */
            }
            tracey_append_buf(&out, &out_len, &out_cap, &c, 1);
            k++;
            if (paste_pending)
                paste_pending = false;
        }
    }

    for (i = 0; i < param_count; i++)
        free(argvals[i]);
    free(argvals);

    /* Also free args? Caller frees. */
    return out;
}

char *tracey_expand_macros(const char *text, struct tracey_macro_table *table,
                                  int *depth)
{
    size_t len = strlen(text);
    size_t out_cap = len + 1;
    size_t out_len = 0;
    char *out = (char *)malloc(out_cap);
    size_t i = 0;

    if (out == NULL)
        return NULL;
    out[0] = '\0';

    (*depth)++;
    if (*depth > 64)
    {
        free(out);
        return strdup(text);
    }

    while (i < len)
    {
        char c = text[i];

        if (c == '"' || c == '\'')
        {
            /* Copy string/char literals verbatim without macro expansion. */
            char quote = c;
            if (out_len + 1 > out_cap)
            {
                out_cap = out_len + 2;
                out = (char *)realloc(out, out_cap);
            }
            out[out_len++] = text[i++];
            out[out_len] = '\0';
            while (i < len)
            {
                if (out_len + 1 > out_cap)
                {
                    out_cap = out_len + 2;
                    out = (char *)realloc(out, out_cap);
                }
                out[out_len++] = text[i];
                out[out_len] = '\0';
                if (text[i] == '\\' && i + 1 < len)
                {
                    i++;
                    if (out_len + 1 > out_cap)
                    {
                        out_cap = out_len + 2;
                        out = (char *)realloc(out, out_cap);
                    }
                    out[out_len++] = text[i];
                    out[out_len] = '\0';
                }
                else if (text[i] == quote)
                {
                    i++;
                    break;
                }
                i++;
            }
            continue;
        }

        if (tracey_is_id_start(c))
        {
            size_t start = i;
            while (i < len && tracey_is_id_cont(text[i]))
                i++;
            size_t name_len = i - start;
            char *name = tracey_strdup_range(text + start, name_len);
            int midx = tracey_macro_find(table, name);

            if (midx >= 0 && table->items[midx].function_like)
            {
                size_t j = i;
                while (j < len && tracey_is_space(text[j]))
                    j++;
                if (j < len && text[j] == '(')
                {
                    size_t end_pos = j;
                    size_t argc = 0;
                    char **args = tracey_split_macro_args(text, j, &end_pos, &argc);

                    char *instantiated = tracey_instantiate_function_macro(&table->items[midx],
                                                                          args, argc, table,
                                                                          depth);
                    /* Free args. */
                    if (args != NULL)
                    {
                        for (size_t a = 0; a < argc; a++)
                            free(args[a]);
                        free(args);
                    }

                    char *expanded = NULL;
                    if (instantiated != NULL)
                    {
                        expanded = tracey_expand_macros(instantiated, table, depth);
                        free(instantiated);
                    }
                    if (expanded == NULL)
                        expanded = strdup("");

                    size_t elen = strlen(expanded);
                    if (out_len + elen + 1 > out_cap)
                    {
                        out_cap = out_len + elen + 1;
                        out = (char *)realloc(out, out_cap);
                    }
                    memcpy(out + out_len, expanded, elen);
                    out_len += elen;
                    out[out_len] = '\0';
                    free(expanded);
                    free(name);
                    i = end_pos;
                    continue;
                }
                /* Not a call: fall through. */
            }

            if (midx >= 0 && !table->items[midx].function_like)
            {
                char *replacement = strdup(table->items[midx].replacement);
                char *expanded = tracey_expand_macros(replacement, table, depth);
                free(replacement);

                if (expanded == NULL)
                    expanded = strdup("");

                size_t elen = strlen(expanded);
                if (out_len + elen + 1 > out_cap)
                {
                    out_cap = out_len + elen + 1;
                    out = (char *)realloc(out, out_cap);
                }
                memcpy(out + out_len, expanded, elen);
                out_len += elen;
                out[out_len] = '\0';
                free(expanded);
                free(name);
                continue;
            }

            free(name);
            /* Not a macro: copy identifier. */
            if (out_len + name_len + 1 > out_cap)
            {
                out_cap = out_len + name_len + 1;
                out = (char *)realloc(out, out_cap);
            }
            memcpy(out + out_len, text + start, name_len);
            out_len += name_len;
            out[out_len] = '\0';
            continue;
        }

        /* Regular character. */
        if (out_len + 1 > out_cap)
        {
            out_cap = out_len + 1;
            out = (char *)realloc(out, out_cap);
        }
        out[out_len++] = c;
        out[out_len] = '\0';
        i++;
    }

    (*depth)--;
    return out;
}

