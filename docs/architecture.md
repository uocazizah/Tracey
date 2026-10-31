# Architecture

## Overview

The Tracey bootstrap compiler is a multi-stage compiler written in C++23.
It compiles `.trc` source files into assembly (and, with the internal
toolchain, into native executables) for AArch64 Linux.

## Pipeline

```
source file
   |
   v
source::read()        -- hardened file loading
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
Located in `general/source/` and `include/tracey/source.hxx`.
Loads a source file into memory with security checks (O_NOFOLLOW on POSIX,
regular-file checks, size limits).

### argument
Located in `general/argument/` and `include/tracey/argument.hxx`.
Command-line parser. Supports `--help`, `--version`, `--verbose`,
`--warnings-as-errors`, `--debug-info`, `-O0..-O3`, `-o`, `-I`, `-D`.

### lexer
Located in `general/lexer/` and `include/tracey/lexer.hxx`.
Byte-oriented lexer producing tokens with line/column information.

### parser
Located in `general/parser/` and `include/tracey/parser.hxx`, `ast.hxx`.
Recursive-descent parser producing an AST.

### semantic
Located in `general/semantic/` and `include/tracey/semantic.hxx`.
Scope-aware symbol table and type checker.

### codegen
Located in `general/codegen/` and `include/tracey/codegen.hxx`.
Emits AArch64 assembly text.

### assembler
Located in `general/assembler/` and `include/tracey/assembler.hxx`.
Minimal AArch64 assembler producing ELF64 relocatable objects.

### linker
Located in `general/linker/` and `include/tracey/linker.hxx`.
Minimal ELF64 AArch64 linker producing executables.

### config
Located in `include/tracey/config.hxx`.
Wraps Kconfig-generated macros into typed constexpr accessors.

## Data flow

1. `index.cxx` reads all input files.
2. Each file is lexed; lexer errors are reported and stop processing that file.
3. Each file is parsed into a `program` AST.
4. Semantic analysis validates the AST.
5. Code generation emits assembly text.
6. Assembly text from all inputs is concatenated.
7. If `--output` is given, the text is written to that file; otherwise it is
   printed to stdout.

When using the internal toolchain, assembly text is further assembled and
linked into an executable.
