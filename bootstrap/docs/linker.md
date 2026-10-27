# Internal Linker

## Overview

`traceyld` is a minimal ELF64 AArch64 linker. It combines relocatable objects
produced by `traceyas` into a statically linked executable.

## Process

1. Parse each input object.
2. Combine `.text` sections.
3. Collect defined symbols (including local labels).
4. Apply relocations.
5. If no `_start` symbol exists, prepend a runtime startup stub that calls
   `main` and invokes the `exit` syscall.
6. Write an ELF64 executable with one `PT_LOAD` segment.

## Entry point

- If `_start` is defined, use it.
- Otherwise use the startup stub (which calls `main`).

## Limitations

- Static linking only.
- No libc or dynamic linking.
- No section headers in output.
- Single text segment.