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
- `if`/`while`/`for` are lowered with labels and conditional branches.
- `return` branches to a function epilogue label.
- Floating-point values use the `s`/`d` registers and `fadd`, `fsub`,
  `fmul`, `fdiv`.
- Pointers and arrays use `ldr`/`str` with byte or word strides.

## Output

Assembly text using GNU assembler syntax (`.text`, `.globl`, labels). The
compiler emits `.loc` line directives for basic debug information.

## Limitations

- Only AArch64 is implemented.
- No optimization is performed.
- Calls with more than 8 arguments are not supported.
- Shift operators (`<<`, `>>`) are parsed but code generation passes the
  left operand through unchanged.
- Bitwise NOT (`~`) is parsed but currently generates `mov x0, #0`.
- `-g`, `-Werror`, `-O`, and `-D` are accepted on the command line but are
  not yet acted on by the compiler.
