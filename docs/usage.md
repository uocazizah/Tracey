# Usage

## Compile to assembly

```bash
./build/traceyc input.trc -o output.s
```

Without `-o`, assembly is printed to stdout.

## Assemble

```bash
./build/traceyas output.s output.o
```

## Link

```bash
./build/traceyld program.elf output.o
chmod +x program.elf
```

## Run

```bash
./program.elf
echo "exit=$?"
```

## Full pipeline example

```bash
./build/traceyc test.trc -o test.s
./build/traceyas test.s test.o
./build/traceyld test.elf test.o
chmod +x test.elf
./test.elf
echo "exit=$?"
```

## Using system toolchain (fallback)

```bash
./build/traceyc test.trc -o test.s
gcc test.s -o test
./test
```

## Verbose mode

```bash
./build/traceyc -v test.trc
```
