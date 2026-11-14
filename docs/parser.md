# Parser and AST

## Grammar

```
program        := function_decl*
function_decl  := type IDENT '(' param_list? ')' (block | ';')
param_list     := param (',' param)* (',' '...')?
param          := type IDENT
type           := ('unsigned' | 'signed' | 'short' | 'long' 'long'?)? base
                 ('*')?
base           := 'int' | 'char' | 'short' | 'long' | 'float' | 'double'
                 | 'bool' | 'void' | 'size_t'

block          := '{' statement* '}'
statement      := var_decl | if_stmt | while_stmt | for_stmt | break_stmt
                 | continue_stmt | goto_stmt | label_stmt | return_stmt
                 | expr_stmt | block | ';'
var_decl       := type IDENT ('[' INTEGER ']')? ('=' expression)? ';'
if_stmt        := 'if' '(' expression ')' statement ('else' statement)?
while_stmt     := 'while' '(' expression ')' statement
for_stmt       := 'for' '(' (var_decl | expr_stmt | ';') expression? ';'
                 expression? ')' statement
break_stmt     := 'break' ';'
continue_stmt  := 'continue' ';'
goto_stmt      := 'goto' IDENT ';'
label_stmt     := IDENT ':' statement
return_stmt    := 'return' expression? ';'
expr_stmt      := expression ';'

expression     := assignment
assignment     := ternary (('=' | '+=' | '-=' | '*=' | '/=' | '%='
                  | '<<=' | '>>=' | '&=' | '|=' | '^=') assignment)?
ternary        := logical_or ('?' expression ':' expression)?
logical_or     := logical_and ('||' logical_and)*
logical_and    := bitwise_or ('&&' bitwise_or)*
bitwise_or     := bitwise_xor ('|' bitwise_xor)*
bitwise_xor    := bitwise_and ('^' bitwise_and)*
bitwise_and    := equality ('&' equality)*
equality       := relational (('==' | '!=') relational)*
relational     := additive (('<' | '>' | '<=' | '>=') additive)*
additive       := multiplicative (('+' | '-') multiplicative)*
multiplicative := unary (('*' | '/' | '%') unary)*
unary          := ('-' | '!' | '~' | '&' | '*') unary | primary
primary        := INTEGER | FLOAT | 'true' | 'false' | CHAR | STRING
                 | IDENT | IDENT '(' arg_list? ')' | '(' expression ')'
                 | IDENT ('[' expression ']')*
arg_list       := expression (',' expression)*
```

Notes:

- Shift operators (`<<`, `>>`) are lexed and have dedicated tokens, but the
  parser does not currently give them a precedence level; they cannot be used
  in expressions yet.
- Casts (`(type) expr`) are not supported.
- `sizeof`, `do`/`while`, `switch`, structs, unions, enums, typedefs, and
  function pointers are not supported.

## AST

AST node types are defined in `include/tracey/ast.h`:

- `program` — list of `function_decl`.
- `function_decl` — return type, name, parameters, body (NULL for a
  prototype ending in `;`).
- `block_stmt` — list of statements.
- `var_decl_stmt` — typed variable declaration with optional initializer and
  array size.
- `expr_stmt` — expression statement.
- `if_stmt` — conditional with optional else.
- `while_stmt` — loop.
- `for_stmt` — loop with optional init, condition, and update.
- `break_stmt`, `continue_stmt`, `goto_stmt`, `label_stmt`.
- `return_stmt` — return with optional value.
- `int_expr`, `bool_expr`, `real_expr`, `string_expr`, `char_expr`,
  `ident_expr`, `unary_expr`, `binary_expr`, `index_expr`, `addr_expr`,
  `deref_expr`, `call_expr`, `ternary_expr`.

## Error recovery

The parser synchronizes on `;`, `}`, and statement-starting keywords after an
error, allowing multiple errors to be reported in one pass.
