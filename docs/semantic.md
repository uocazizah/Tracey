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
are represented as pointers to their element type. Basic `typedef` aliases
are resolved by the parser to one of these types before analysis, so they
introduce no new type. A file-scope `struct` definition introduces a struct
value type; a symbol of that type records the index of the resolved
definition, which member access uses to find member types. A function
pointer type records the pointee's return type and parameter types in a
`tracey_func_sig`.

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
- `if`/`while`/`do`/`for` conditions must be `bool`.
- `switch` conditions must be an integer type, and case values must be
  unique.
- `return` value must match the function return type exactly.
- `void` functions must not return a value.
- `break` must be inside a loop or a `switch`; `continue` must be inside a
  loop.
- `goto` labels must be declared and used (within a function).
- Array indices must be `int`.
- Struct member access requires a struct value and resolves the member name in
  the corresponding struct definition. Fixed-size arrays of structs and
  positional brace initializers are supported; compound assignment to a struct
  member is rejected, and struct parameters, return values, and pointers are
  not supported.
- A bare function name decays to a function pointer value. Calls through a
  function-pointer variable check arity and parameter types against the
  recorded signature.

## Global functions

All top-level functions are declared in the global scope before analysis, so
forward references are allowed.

## Built-ins

`print(...)` accepts any argument and prints it (`int`/`char` as a number,
strings and `char*` as text). `printf(fmt, ...)` formats a literal format
string with `%d` and `%s`. `puts(s)` writes a `char*` followed by a newline,
`putchar(c)` writes one character, and `getchar()` returns the next byte
(-1 at end of input). `fget(buf)` reads a line into a `char*` buffer with no
size limit; `fgets(buf, size)` reads a bounded line. `scanf(fmt, ...)` reads
one value according to a `char*` format string: a leading `%d` reads an
integer and any other conversion is treated as a string. `scanfs(fmt, ...)`
is the bounded variant and takes the destination buffer size after the
pointer. Neither `scanf` variant handles more than the first conversion.
`input_int()` returns an `int` (0 on EOF or error). `input_str(buf, size)`
reads a line into a `char*` buffer and returns the number of bytes read.

## Diagnostics

Semantic errors are reported as `path:line:column: message`.
