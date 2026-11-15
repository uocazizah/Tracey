# Parser and AST

## Grammar

```
program        := (typedef_decl | struct_decl | function_decl)*
typedef_decl   := 'typedef' type IDENT ';'
struct_decl    := 'struct' IDENT '{' struct_member* '}' ';'
struct_member  := type IDENT ';'
function_decl  := type IDENT '(' param_list? ')' (block | ';')
param_list     := param (',' param)* (',' '...')?
param          := type IDENT
type           := ('unsigned' | 'signed' | 'short' | 'long' 'long'?)? base
                 ('*')?
base           := 'int' | 'char' | 'short' | 'long' | 'float' | 'double'
                 | 'bool' | 'void' | 'size_t' | 'struct' IDENT | typedef_name

block          := '{' statement* '}'
statement      := var_decl | if_stmt | while_stmt | do_while_stmt
                 | switch_stmt | for_stmt | break_stmt
                 | continue_stmt | goto_stmt | label_stmt | return_stmt
                 | expr_stmt | block | ';'
var_decl       := type declarator ('[' INTEGER ']')? ('=' (expression | '{' expression (',' expression)* '}'))? ';'
declarator     := IDENT | '(' '*' IDENT ')' '(' param_list? ')'
if_stmt        := 'if' '(' expression ')' statement ('else' statement)?
while_stmt     := 'while' '(' expression ')' statement
do_while_stmt  := 'do' statement 'while' '(' expression ')' ';'
switch_stmt    := 'switch' '(' expression ')' '{' switch_item* '}'
switch_item    := ('case' INTEGER ':' | 'default' ':') statement*
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
relational     := shift (('<' | '>' | '<=' | '>=') shift)*
shift          := additive (('<<' | '>>') additive)*
additive       := multiplicative (('+' | '-') multiplicative)*
multiplicative := unary (('*' | '/' | '%') unary)*
unary          := ('-' | '!' | '~' | '&' | '*' | '++' | '--') unary
                  | 'sizeof' ( '(' type ')' | unary )
                  | postfix
postfix        := primary ('[' expression ']' | '.' IDENT)* ('++' | '--')?
primary        := INTEGER | FLOAT | 'true' | 'false' | CHAR | STRING
                  | IDENT | IDENT '(' arg_list? ')' | '(' expression ')'
arg_list       := expression (',' expression)*
```

Notes:

- Casts (`(type) expr`) are not supported.
- `sizeof` accepts both `sizeof(type)` and `sizeof expr`, but the bootstrap
  parser replaces it with the fixed value 8. It does not compute the real
  size of the operand, so all scalar types report the same size.
- `switch` cases are independent: after a case body runs, control leaves the
  switch and does not fall through into the next case. `break` exits the
  switch. Case labels must be integer literals.
- `typedef` records basic type aliases (`typedef int Integer;`) at file scope
  for use in later declarations. Union, enum, and function-pointer typedefs
  are not supported yet.
- `struct` definitions may appear only at file scope (`struct Point { ... };`).
  A struct tag must be defined before it is used, and struct members may be
  scalars or other struct types, but not arrays or pointers. Struct-typed
  expressions support member access (`p.x`, `o.in.a`), fixed-size arrays of
  structs (`a[i].x`), and positional brace initializers. Struct parameters,
  return values, and pointers are not supported yet.
- Function pointers use the `int (*name)(int, int)` declarator and may be
  called through the variable. A bare function name decays to a function
  pointer value. Function-pointer parameters are supported; return values
  and typedefs of function pointers are not.
- Unions, enums, and function pointers are not supported.

## AST

AST node types are defined in `include/tracey/ast.h`:

- `program` — list of `function_decl` plus the `struct_def` definitions they
  may refer to.
- `function_decl` — return type, name, parameters, body (NULL for a
  prototype ending in `;`).
- `struct_def` — struct tag, member list, and total size. Each member records
  its type, optional struct id, name, and byte offset.
- `block_stmt` — list of statements.
- `var_decl_stmt` — typed variable declaration with optional initializer,
  array size, and (for struct types) a struct id.
- `expr_stmt` — expression statement.
- `if_stmt` — conditional with optional else.
- `while_stmt` — loop tested before the body.
- `do_while_stmt` — loop whose body runs before the condition is tested.
- `switch_stmt` — condition with independent case bodies and an optional
  default body.
- `for_stmt` — loop with optional init, condition, and update.
- `break_stmt`, `continue_stmt`, `goto_stmt`, `label_stmt`.
- `return_stmt` — return with optional value.
- `int_expr`, `bool_expr`, `real_expr`, `string_expr`, `char_expr`,
  `ident_expr`, `unary_expr`, `binary_expr`, `index_expr`, `addr_expr`,
  `deref_expr`, `call_expr`, `ternary_expr`, `member_expr`.

Prefix and postfix `++`/`--` are represented as `unary_expr` nodes. `sizeof`
is resolved by the parser into an `int_expr` holding the fixed size.

## Error recovery

The parser synchronizes on `;`, `}`, and statement-starting keywords after an
error, allowing multiple errors to be reported in one pass.
