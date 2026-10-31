# Semantic Analysis

## Overview

The semantic analyzer (`include/tracey/semantic.hxx`, `general/semantic/semantic.cxx`)
validates the AST before code generation.

## Symbol table

A stack of scopes is maintained. Each scope maps names to symbols. Symbols
record type, function-ness, and arity.

## Checks performed

- Variable redeclaration in the same scope.
- Undefined identifiers.
- Function calls: existence, arity.
- Type checking for:
  - arithmetic: `int` operands, `int` result.
  - comparisons: `int` operands, `bool` result.
  - logical: `bool` operands, `bool` result.
  - equality: same types.
  - assignment: matching types.
  - unary `-`: `int`; `!`: `bool`.
- `if`/`while` conditions must be `bool`.
- `return` value must match function return type.
- `void` functions must not return a value.

## Global functions

All top-level functions are declared in the global scope before analysis, so
forward references are allowed.

## Diagnostics

Semantic errors are reported as `path:line:column: message`.
