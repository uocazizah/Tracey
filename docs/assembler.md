# Internal Assembler

## Overview

`traceyas` is a minimal AArch64 assembler. It parses the assembly text
produced by the code generator and emits ELF64 relocatable objects.

## Supported instructions

`mov`, `add`, `sub`, `mul`, `sdiv`, `msub`, `cmp`, `cset`, `neg`, `ret`,
`b`, `beq`, `bl`, `stp`, `ldp`, `ldr`, `str`.

## Directives

- `.text`
- `.globl name`
- `label:`

## Output format

ELF64 relocatable object (`ET_REL`) with sections:
- `.text`
- `.symtab`
- `.strtab`
- `.rela.text`
- `.shstrtab`

## Relocations

- `R_AARCH64_CALL26` (283) for `bl`.
- `R_AARCH64_JUMP26` (282) for `b`.
- `R_AARCH64_CONDBR19` (280) for `beq`.

## Limitations

- Only the instruction forms used by the code generator are guaranteed.
- No macro or advanced directive support.