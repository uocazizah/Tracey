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
```

Input (stdin only):

```trc
int x = input_int();        // returns 0 on EOF or error
char buf[32];
input_str(buf, 32);        // reads a line, null-terminates, returns count
print(buf);
```

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
