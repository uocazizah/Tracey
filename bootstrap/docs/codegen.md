# Code Generation

## Target

AArch64 Linux (System V-like AAPCS64).

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
- `if`/`while` are lowered with labels and conditional branches.
- `return` branches to a function epilogue label.

## Output

Assembly text using GNU assembler syntax (`.text`, `.globl`, labels).

## Limitations

- Only AArch64 is implemented.
- No optimization is performed.
- Calls with more than 8 arguments are not supported.