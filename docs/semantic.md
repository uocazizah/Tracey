# Semantic Analysis

## Overview

The semantic analyzer (`include/tracey/semantic.h`, `general/semantic/semantic.c`)
validates the AST before code generation.

## Symbol table

A stack of scopes is maintained. Each scope maps names to symbols. Symbols
record type, function-ness, and arity (for functions).

## Types

Integer types: `int`, `unsigned int`, `short`, `unsigned short`, `long`,
`unsigned long`, `long long`, `unsigned long long`, `char`, `size_t`.
Floating-point types: `float`, `double`. Plus `bool`, `void`, and pointers
to `int`, `char`, `float`, `double` (and the other integer types). Arrays
are represented as pointers to their element type.

## Checks performed

- Variable redeclaration in the same scope.
- Undefined identifiers.
- Function calls: existence, arity.
- Type checking for:
  - arithmetic: numeric operands, result follows the wider operand
    (double > float > long long > long > size_t > int).
  - comparisons: numeric operands, `bool` result.
  - logical: `bool` operands, `bool` result.
  - bitwise and shifts: integer operands.
  - equality: same types.
  - assignment: matching types, with integer-to-integer assignment allowed
    (including possible narrowing).
  - unary `-`: numeric; `!`: `bool`; `~`: integer.
- `if`/`while`/`for` conditions must be `bool`.
- `return` value must match the function return type exactly.
- `void` functions must not return a value.
- `break`/`continue` must be inside a loop.
- `goto` labels must be declared and used (within a function).
- Array indices must be `int`.

## Global functions

All top-level functions are declared in the global scope before analysis, so
forward references are allowed.

## Built-ins

`print(...)` accepts any argument and prints it (`int`/`char` as a number,
strings and `char*` as text). `printf(fmt, ...)` formats a literal format
string with `%d` and `%s`. `input_int()` returns an `int` (0 on EOF or
error). `input_str(buf, size)` reads a line into a `char*` buffer and
returns the number of bytes read.

## Diagnostics

Semantic errors are reported as `path:line:column: message`.
