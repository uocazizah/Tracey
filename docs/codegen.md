# Code Generation

## Target

AArch64 Linux (System V-like AAPCS64). The generated code is AArch64
regardless of the host the compiler runs on.

## Conventions

- Return value: `x0`.
- Arguments: `x0`..`x7`.
- Frame pointer: `x29`.
- Link register: `x30`.
- Stack pointer is 16-byte aligned.

## Strategy

- Each function gets a stack frame holding local variables (8 bytes each).
- Expressions are evaluated with a stack-based temporary scheme; results are
  left in `x0`.
- `if`/`while`/`do`/`for` are lowered with labels and conditional branches.
- `switch` is lowered to a chain of comparisons against each case value.
  Each case body is independent and branches to the end of the switch after
  it runs, so there is no fall-through.
- Prefix and postfix `++`/`--` load the operand, adjust it, store it back,
  and leave the new value (prefix) or the old value (postfix) in `x0`.
- `return` branches to a function epilogue label.
- Floating-point values use the `s`/`d` registers and `fadd`, `fsub`,
  `fmul`, `fdiv`.
- Pointers and arrays use `ldr`/`str` with byte or word strides.
- Struct locals reserve `struct_def.size` bytes. Member access computes the
  member address through `cg_gen_addr` and then loads or stores eight bytes,
  because every member is eight-byte sized and aligned in this stage.

## Output

Assembly text using GNU assembler syntax (`.text`, `.globl`, labels). The
compiler emits `.loc` line directives for basic debug information.

## Limitations

- Only AArch64 is implemented.
- No optimization is performed.
- Calls with more than 8 arguments are not supported.
- `sizeof` is resolved by the parser to the fixed value 8 for every scalar
  type; the emitted code does not depend on the operand type.
- Struct member access loads and stores eight bytes per member regardless of
  the declared member type, mirroring the eight-byte member layout.
- `switch` cases do not fall through; each case body branches to the end of
  the switch.
- `scanf`/`scanfs` honour only the first `%d` or `%s` conversion.
- Shift operators (`<<`, `>>`) are parsed but code generation passes the
  left operand through unchanged.
- Bitwise NOT (`~`) is parsed but currently generates `mov x0, #0`.
- `-g`, `-Werror`, `-O`, and `-D` are accepted on the command line but are
  not yet acted on by the compiler.
