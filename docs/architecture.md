# Architecture

## Overview

The Tracey bootstrap compiler is a multi-stage compiler written in ISO C17.
It compiles `.trc` source files into assembly, and (with the internal
toolchain) into native AArch64 Linux executables.

## Pipeline

```
source file
   |
   v
source::read()        -- hardened file loading
   |
   v
preprocessor          -- #include, #define, conditionals
   |
   v
lexer                 -- tokens
   |
   v
parser                -- AST (program)
   |
   v
semantic analyzer     -- typed, validated AST
   |
   v
code generator        -- AArch64 assembly text
   |
   v
assembler (optional)  -- ELF64 relocatable object
   |
   v
linker (optional)     -- ELF64 executable
```

## Modules

### source
Located in `general/source/` and `include/tracey/source.h`.
Loads a source file into memory with security checks (O_NOFOLLOW on POSIX,
regular-file checks, size limits).

### argument
Located in `general/argument/` and `include/tracey/argument.h`.
Command-line parser. Supports `--help`, `--version`, `--verbose`,
`--warnings-as-errors`, `--debug-info`, `-O0..-O3`, `-o`, `-I`, `-D`,
`-S`/`--assembly`.

### preprocessor
Located in `general/preprocessor/` and `include/tracey/preprocessor.h`.
Textual preprocessor: `#include`, `#define` (object-like and
function-like macros with multiple and variadic parameters, token
pasting `##` and stringification `#`), `#undef`, `#error`,
`#warning`, `#ifdef`, `#ifndef`, `#if`, `#elif`, `#else`,
`#endif`, `#defined`.

### lexer
Located in `general/lexer/` and `include/tracey/lexer.h`.
Byte-oriented lexer producing tokens with line/column information.

### parser
Located in `general/parser/` and `include/tracey/parser.h`, `ast.h`.
Recursive-descent parser producing an AST.

### semantic
Located in `general/semantic/` and `include/tracey/semantic.h`.
Scope-aware symbol table and type checker.

### codegen
Located in `general/codegen/` and `include/tracey/codegen.h`.
Emits AArch64 assembly text.

### assembler
Located in `general/assembler/` and `include/tracey/assembler.h`.
Minimal AArch64 assembler producing ELF64 relocatable objects.

### linker
Located in `general/linker/` and `include/tracey/linker.h`.
Minimal ELF64 AArch64 linker producing executables.

### runtime
Located in `general/runtime/` and `include/tracey/runtime.h`.
Emits the syscall-based runtime helpers (print, input, etc.) used by
generated programs.

### config
Located in `include/tracey/config.h`.
Wraps Kconfig-generated macros into typed constexpr accessors.

## Data flow

1. `tools/traceyc/main.c` reads all input files.
2. Each file is preprocessed; preprocessor errors stop that file.
3. Each file is lexed; lexer errors are reported and stop that file.
4. Each file is parsed into a `program` AST.
5. Semantic analysis validates the AST.
6. Code generation emits assembly text.
7. Assembly from all inputs is concatenated.
8. With `-S`, the text is written to the output file (or stdout). Otherwise
   the internal assembler and linker turn it into an executable, named
   `a.out` by default.

When using the internal toolchain, assembly text is further assembled and
linked into an executable.
