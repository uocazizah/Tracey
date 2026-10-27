# Lexer

Specification and implementation notes for the bootstrap compiler's lexer
(`bootstrap/include/tracey/lexer.hxx`, `bootstrap/general/lexer/`).

This document is the reference for what the lexer currently accepts. The
parser and later stages must agree with it; when a rule here changes, update
the unit tests in `bootstrap/test/test_lexer.cxx` in the same change.

## Model

- Input is an in-memory `std::string_view` produced by `tracey::source`. The
  lexer is byte-oriented: a byte ≥ 0x80 is an invalid character for now
  (UTF-8 identifiers are future work).
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
bool   else   false  if     int
return true   void   while
```

Only an exact match with an identifier's text is a keyword: `intx`, `return0`
and `_int` are identifiers. The lookup table is a linear scan (fine at this
size); entries are kept sorted so a future binary search or switch-based
dispatch is a mechanical change.

### Identifiers

`[A-Za-z_][A-Za-z0-9_]*`

### Integers

Decimal digits only: `[0-9]+`. Leading zeros are currently accepted
(`007` is one token). A letter or underscore immediately after the digits is
an error (`invalid_number_suffix`), and the whole run is consumed so the
diagnostic can show it — `123abc` and `0x10` are reported, never silently
split into two tokens. Hexadecimal, binary, floating point and suffixes are
future work.

### Strings

Double-quoted, single-line (`"..."`). The raw lexeme includes both quotes;
decoding escapes is the parser's job. Accepted escapes:

```
\n  \t  \r  \\  \"  \0
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

A slash is division unless it opens a comment. A lone `&` or `|` is an
invalid character for now (bitwise operators are future work).

### Not yet implemented

Hex/binary integers, floats, char literals (`'a'`), bitwise and shift
operators, compound assignment (`+=`, `-=`, ...), increment/decrement,
question mark/colon. Add them here first, then in the lexer, then in tests.

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
| `unterminated_string`   | the opening quote       | quote up to line end  |
| `unterminated_comment`  | the opening `/*`        | comment up to EOF     |

The `traceyc` driver prints one line per error to stderr:

```
Error: path/to/file.trc:3:14: unexpected character '='
```

Non-printable bytes in a message are escaped (`\n`, `\t`, `\xNN`). The driver
reports at most 10 lexer errors per file and exits with status 1 if any were
seen. The token dump is printed only with `--verbose`.

## Testing

Unit tests live in `bootstrap/test/test_lexer.cxx` (doctest, vendored under
`bootstrap/dependencies/doctest`), including a deterministic fuzz-lite case
that asserts lexeme bounds and progress on arbitrary bytes. Run:

```
cmake --build <build-dir> -j
ctest --test-dir <build-dir> --output-on-failure
```
