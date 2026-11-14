# Internal Assembler

## Overview

`traceyas` is a minimal AArch64 assembler. It parses the assembly text
produced by the code generator and emits ELF64 relocatable objects.

## Supported instructions

Arithmetic and logical: `mov`, `movk`, `add`, `sub`, `mul`, `msub`,
`udiv`, `sdiv`, `neg`, `cmp`, `cset`, `and`, `orr`, `eor`.

Floating point: `fmov`, `fneg`, `fcmp`, `fadd`, `fsub`, `fmul`, `fdiv`.

Control flow: `ret`, `b`, `beq`, `bl`, `b.<cond>`, `cbz`, `cbnz`.

Memory: `stp`, `ldp`, `ldr`, `str`, `ldrb`, `strb`, `adr`.

System: `svc`.

## Directives

- `.text`, `.data`
- `.globl name`
- `.file`, `.loc`
- `.asciz`
- `label:`

Other directives are ignored.

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
- `R_AARCH64_CONDBR19` (280) for `beq` and conditional branches.

## Limitations

- Only the instruction forms used by the code generator are guaranteed.
- No macro or advanced directive support.
- Shift instructions (`lsl`, `lsr`, `asr`) and `mvn` are not supported.
