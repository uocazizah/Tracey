# Build Instructions

## Requirements

- C++23 compiler (GCC or Clang)
- Python 3 (for Kconfig)
- Make or CMake

## Make

```bash
cd .
make                      # build traceyc, traceyas, traceyld, tests (silent by default)
make V=1                  # verbose build
make test                 # run unit tests
make clean                # remove build directory
make menuconfig           # interactive Kconfig
make menuconfig-minimal   # use minimal preset (no assembler/linker features)
make menuconfig-default   # use default preset
make menuconfig-all        # enable all features
make olddefconfig         # apply defaults for new options
make help                 # show targets and variables
```

### Non-interactive configuration

If `.config` is missing, `make` automatically generates it from the Kconfig
defaults (equivalent to `make olddefconfig`), so a plain `make` works out of
the box. Use presets to start from a known configuration:
```bash
make menuconfig-default
make
```

`make menuconfig`, `make help` and `make clean` do not require `.config`.

### Silent build

By default the build is quiet (Linux-kernel style). Set `V=1` for full commands:
```bash
make V=1
```

### Auto-detected environment

Architecture is auto-detected from `uname -m` unless overridden:
```bash
make ARCH=x86_64
```

Build outputs:
- `build/traceyc` — compiler
- `build/traceyas` — assembler
- `build/traceyld` — linker
- `build/tracey_lexer_test` — unit tests

## CMake

```bash
cd .
cmake -S . -B build
cmake --build build
ctest --test-dir build
```

## Configuration

Kconfig options are stored in `.config`. Edit with `make menuconfig` or by
editing `.config` directly. Key options:

- `TRACEY_DEBUG_TRACE` — enable runtime tracing.
- `TRACEY_MAX_SOURCE_SIZE` — maximum source file size.
- `TRACEY_STACK_SIZE` — internal stack size.
- `TRACEY_DEFAULT_TARGET_*` — default target architecture.
- `TRACEY_USE_ZLIB` — enable zlib.
- `TRACEY_USE_THREADS` — enable threads.

## Sanitizers

```bash
make ASAN=1 UBSAN=1
```
