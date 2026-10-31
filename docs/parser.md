# Parser and AST

## Grammar (minimal subset)

```
program        := function_decl*
function_decl  := type IDENT '(' param_list? ')' block
param_list     := param (',' param)*
param          := type IDENT
type           := 'int' | 'bool' | 'void'
block          := '{' statement* '}'
statement      := var_decl | if_stmt | while_stmt | return_stmt | expr_stmt | block
var_decl       := type IDENT ('=' expression)? ';'
if_stmt        := 'if' '(' expression ')' statement ('else' statement)?
while_stmt     := 'while' '(' expression ')' statement
return_stmt    := 'return' expression? ';'
expr_stmt      := expression ';'
expression     := assignment
assignment     := IDENT '=' expression | logical_or
logical_or     := logical_and ('||' logical_and)*
logical_and    := equality ('&&' equality)*
equality       := relational (('==' | '!=') relational)*
relational     := additive (('<' | '>' | '<=' | '>=') additive)*
additive       := multiplicative (('+' | '-') multiplicative)*
multiplicative := unary (('*' | '/' | '%') unary)*
unary          := ('-' | '!') unary | primary
primary        := INTEGER | 'true' | 'false' | IDENT | IDENT '(' arg_list? ')' | '(' expression ')'
arg_list       := expression (',' expression)*
```

## AST

AST node types are defined in `include/tracey/ast.hxx`:

- `program` — list of `function_decl`.
- `function_decl` — return type, name, parameters, body.
- `block_stmt` — list of statements.
- `var_decl_stmt` — typed variable declaration with optional initializer.
- `expr_stmt` — expression statement.
- `if_stmt` — conditional with optional else.
- `while_stmt` — loop.
- `return_stmt` — return with optional value.
- `int_expr`, `bool_expr`, `ident_expr`, `unary_expr`, `binary_expr`, `call_expr`.

## Error recovery

The parser synchronizes on `;`, `}`, and statement-starting keywords after an
error, allowing multiple errors to be reported in one pass.
