# Lexer

Specification and implementation notes for the bootstrap compiler's lexer
(`include/tracey/lexer.h`, `general/lexer/`).

This document is the reference for what the lexer currently accepts. The
parser and later stages must agree with it; when a rule here changes, update
the unit tests in `test/test_lexer.c` in the same change.

## Model

- Input is an in-memory `struct tracey_string_view` produced by
  `tracey::source`. The lexer is byte-oriented: a byte at or above 0x80 is
  an invalid character for now (UTF-8 identifiers are future work).
- Token lexemes are non-owning views into the source buffer; they stay valid
  for as long as the source object does.
- Positions are 1-based: `line` counts `\n`, `column` counts bytes on the
  current line. A tab advances the column by one. `\r\n` works because `\r`
  is whitespace.
- Line/column are tracked incrementally: a token's start position is
  snapshotted before its first byte is consumed, so locating a token is O(1).
- `peek()` scans at most one token ahead and caches it; `next()` consumes the
  cache before scanning. `is_at_end()` reports "no bytes left" — not "the next
  token is EOF" (see the header comment for the distinction).
- Blank lines are handled correctly: an empty line expands to an empty
  string and is passed through to the lexer.

## Trivia

Trivia produces no tokens; it is skipped while keeping positions accurate.

| Item             | Rule                                                            |
|------------------|-----------------------------------------------------------------|
| Whitespace       | space, tab, LF, CR, VT (`\v`), FF (`\f`)                        |
| Line comment     | `//` up to (not including) the end of the line                  |
| Block comment    | `/*` ... `*/`; **not nested**; may span lines                   |

An unterminated block comment is a lexical error anchored at its opening
`/*`, not at end-of-input.

## Tokens

### Keywords

```
bool     break  case   char   continue default
do       double else   false  float    for
goto     if     int    long   return   short
signed   size_t sizeof struct switch   true
typedef  unsigned void  while
```

Only an exact match with an identifier's text is a keyword: `intx`,
`return0`, and `_int` are identifiers. The lookup table is a linear scan
(fine at this size); entries are kept sorted so a future binary search or
switch-based dispatch is a mechanical change.

### Identifiers

`[A-Za-z_][A-Za-z0-9_]*`

### Integers

Decimal digits: `[0-9]+`. The lexer also recognises hexadecimal (`0x`/`0X`)
and binary (`0b`/`0B`) prefixes. Leading zeros are accepted (`007` is one
token). A letter or underscore immediately after the digits is an error
(`invalid_number_suffix`), and the whole run is consumed so the diagnostic
can show it — `123abc` and `0x10` are reported, never silently split into
two tokens.

Note: the parser currently decodes decimal integer literals only. Hexadecimal
and binary prefixes are accepted by the lexer but are not yet converted
correctly, so avoid them in source for now.

### Floating point

Decimal floating-point literals: `1.5`, `1e3`, `2.5f`. The `f`/`F` suffix
marks a single-precision value.

### Characters

Single-quoted, single-line (`'a'`). Escapes are the same as for strings.
Multi-character constants are not specially handled.

### Strings

Double-quoted, single-line (`"..."`). The raw lexeme includes both quotes;
decoding escapes is the parser's job. Accepted escapes:

```
\n  \t  \r  \\  \"  \0  \'
```

- An invalid escape is an error anchored at the offending backslash; the
  string is still consumed to its closing quote so the rest of the file
  lexes normally.
- A newline or end of input before the closing quote is an
  `unterminated_string` error anchored at the opening quote.

### Punctuation and operators

| Lexeme | Token          | Lexeme | Token           |
|--------|----------------|--------|-----------------|
| `(`    | `lparen`       | `=`    | `assign`        |
| `)`    | `rparen`       | `+`    | `plus`          |
| `{`    | `lbrace`       | `-`    | `minus`         |
| `}`    | `rbrace`       | `*`    | `star`          |
| `[`    | `lbracket`     | `/`    | `slash`         |
| `]`    | `rbracket`     | `%`    | `percent`       |
| `;`    | `semicolon`    | `!`    | `bang`          |
| `,`    | `comma`        | `<`    | `less`          |
| `.`    | `dot`          | `>`    | `greater`       |
| `==`   | `equal_equal`  | `<=`   | `less_equal`    |
| `!=`   | `bang_equal`   | `>=`   | `greater_equal` |
| `&&`   | `amp_amp`      | `\|\|` | `pipe_pipe`     |
| `&`    | `ampersand`    | `\|`   | `pipe`          |
| `^`    | `caret`        | `~`    | `tilde`         |
| `++`   | `plus_plus`    | `--`   | `minus_minus`   |
| `<<`   | `less_less`    | `>>`   | `greater_greater` |
| `?`    | `question`     | `:`    | `colon`         |
| `+=` `-=` `*=` `/=` `%=` | compound assign | `<<=` `>>=` `&=` `\|=` `^=` | compound assign |

A slash is division unless it opens a comment. A lone `&` or `|` is the
bitwise operator, not an error.

### Not yet implemented

Hex/binary integer value conversion in the parser, char literals wider than
one byte, and bitwise shift code generation (the operators are lexed but the
parser does not give them a precedence level, so they cannot be used in
expressions).

## Errors and recovery

The lexer never stops at the first error. An error is reported as a token
with `type == token_type::error_` plus a `token_error` kind, and scanning
continues. Every token (error or not) consumes at least one byte, which
guarantees progress and termination.

| Error kind              | Reported at             | Lexeme                |
|-------------------------|-------------------------|-----------------------|
| `invalid_character`     | the offending byte      | that byte             |
| `invalid_number_suffix` | first digit             | digits plus suffix    |
| `invalid_escape`        | the backslash           | `\` + escaped byte    |
| `invalid_char_escape`   | the backslash           | `\` + escaped byte    |
| `unterminated_string`   | the opening quote       | quote up to line end  |
| `unterminated_char`     | the opening quote       | quote up to line end  |
| `unterminated_comment`  | the opening `/*`        | comment up to EOF     |

The `traceyc` driver prints one line per error to stderr:

```
Error: path/to/file.trc:3:14: unexpected character '='
```

Non-printable bytes in a message are escaped (`\n`, `\t`, `\xNN`). The driver
reports at most 10 lexer errors per file and exits with status 1 if any were
seen. The token dump is printed only with `--verbose`.

## Testing

Unit tests live in `test/test_lexer.c` (built with the repository's small
C17 harness in `test/test_harness.h`), including a deterministic fuzz-lite
case that asserts lexeme bounds and progress on arbitrary bytes. Run:

```
make test
```
