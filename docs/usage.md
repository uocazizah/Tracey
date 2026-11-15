# Usage

By default `traceyc` runs the whole pipeline and writes an executable.

## Compile to an executable

```bash
./build/traceyc input.trc -o output
./output
echo "exit=$?"
```

Without `-o`, the executable is named `a.out`.

## Emit assembly instead

```bash
./build/traceyc input.trc -S -o output.s
```

Without `-o`, assembly is printed to stdout.

## Verbose mode

```bash
./build/traceyc -V test.trc
```

## Multiple input files

```bash
./build/traceyc a.trc b.trc -o program
```

All inputs are compiled and their assembly is concatenated before linking.

## Include paths and macros

```bash
./build/traceyc test.trc -I include -D NAME=value -o test
```

`-I` adds a search directory for `#include`. `-D` is accepted on the
command line but is not yet applied to the preprocessor; define macros in
source with `#define` instead.

## Standard I/O

Output:

```trc
print(...);                 // variadic; ints/chars use print_int,
                            // char*/strings use print_str
printf(fmt, ...);           // %d and %s in a literal format string
puts(s);                    // write a char* followed by a newline
putchar(c);                 // write one character
```

Input (stdin only):

```trc
int c = getchar();          // next byte; -1 at end of input
int x = input_int();        // returns 0 on EOF or error
char buf[32];
input_str(buf, 32);         // reads a line, null-terminates, returns count
fget(buf);                  // reads a line with no size limit
fgets(buf, 32);             // reads a bounded line
scanf("%d", &x);            // reads an integer
scanfs("%s", buf, 32);      // reads a string into a bounded buffer
print(buf);
```

`scanf` reads a single value: a leading `%d` reads an integer and any other
conversion is read as a string. `scanfs` is the bounded variant and takes
the destination buffer size after the pointer. Neither handles more than the
first conversion.

## Structs

```trc
struct Point
{
    int x;
    int y;
};

int main()
{
    struct Point p;
    p.x = 3;
    p.y = 4;
    return p.x + p.y;
}
```

A struct definition must appear at file scope, and its tag must be defined
before use. Local struct variables support member read and write, including
nested access through a struct-typed member (`o.in.a`). Struct arrays,
initializers, parameters, return values, and pointers are not supported yet,
and every member occupies eight bytes because there is no packing or
alignment analysis.

## Function pointers

```trc
int add(int a, int b) { return a + b; }
int mul(int a, int b) { return a * b; }

int main()
{
    int (*op)(int, int);
    op = add;
    print(op(3, 4));   // 7
    op = mul;
    print(op(3, 4));   // 12
    return 0;
}
```

A function name decays to a function pointer value, so it can be passed to
a function-pointer parameter as well.

## Manual toolchain

If you want to drive the tools yourself:

```bash
./build/traceyc test.trc -S -o test.s
./build/traceyas test.s test.o
./build/traceyld test.elf test.o
chmod +x test.elf
./test.elf
echo "exit=$?"
```

## Using the system toolchain

```bash
./build/traceyc test.trc -S -o test.s
gcc test.s -o test
./test
```

This only works on hosts where GCC can assemble AArch64.
