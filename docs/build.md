# Build Instructions

## Requirements

- ISO C17 compiler (GCC or Clang)
- Python 3 (for Kconfig)
- GNU Make

## Make

```bash
make                      # build traceyc, traceyas, traceyld, tests
make V=1                  # verbose build
make test                 # run unit tests and end-to-end language tests
make test-e2e             # run only the end-to-end language tests
make clean                # remove build directory
make menuconfig           # interactive Kconfig
make menuconfig-minimal   # use minimal preset (no assembler/linker features)
make menuconfig-default   # use default preset
make menuconfig-all       # enable all features
make olddefconfig         # apply defaults for new options
make help                 # show targets and variables
make install              # install binaries, libraries, and stdlib
```

### Non-interactive configuration

If `.config` is missing, `make` automatically generates it from the Kconfig
defaults (equivalent to `make olddefconfig`), so a plain `make` works out of
the box. Use presets to start from a known configuration:

```bash
make menuconfig-default
make
```

`make menuconfig`, `make help`, and `make clean` do not require `.config`.

### Silent build

By default the build is quiet (Linux-kernel style). Set `V=1` for full
commands:

```bash
make V=1
```

### Host architecture

The compiler itself builds on any POSIX host, but the code it generates is
AArch64. The end-to-end language tests compile and run the generated
binaries, so they only run on AArch64 hosts. On other hosts `make test`
skips that step instead of failing. You can override the detected host
architecture with `ARCH=...` (for example `make ARCH=x86_64`), which
affects the compiler's own build flags; it does not change the AArch64
target of the generated code.

### Sanitizers

```bash
make ASAN=1 UBSAN=1
```

## Build outputs

- `build/traceyc` — compiler
- `build/traceyas` — assembler
- `build/traceyld` — linker
- `build/tracey_lexer_test`, `build/tracey_source_test`,
  `build/tracey_argument_test`, `build/tracey_parser_test`,
  `build/tracey_semantic_test` — unit tests

## Configuration

Kconfig options are stored in `.config`. Edit with `make menuconfig` or by
editing `.config` directly. Key options:

- `TRACEY_DEBUG_TRACE` — enable runtime tracing.
- `TRACEY_MAX_SOURCE_SIZE` — maximum source file size.
- `TRACEY_STACK_SIZE` — internal stack size.
- `TRACEY_MAX_LEXER_ERRORS` — lexer errors reported per file.
- `TRACEY_DEFAULT_TARGET_*` — reported default target architecture.
- `TRACEY_USE_ZLIB` — enable zlib.
- `TRACEY_USE_THREADS` — enable threads.
- `TRACEY_MODULE_*` — enable or disable compiler modules.
