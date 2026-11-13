# mk/toolchain.mk - Compiler and build flags.
#
# Tracey is written in ISO C17. Only a C compiler is required; no C++
# toolchain is used anywhere in the build.

CC ?= cc
# Use ccache if available for faster rebuilds.
CCACHE := $(shell command -v ccache 2>/dev/null)
ifneq ($(CCACHE),)
CC := $(CCACHE) $(CC)
endif

CSTD ?= -std=c17
PROJECT_VERSION ?= 1.0.0
BUILD_DIR ?= build

WARN_FLAGS := -Wall -Wextra -Wpedantic -Wshadow -Wunused -Wconversion -Wsign-conversion \
              -Wformat=2 -Wimplicit-fallthrough -Wmisleading-indentation -Wduplicated-cond \
              -Wduplicated-branches -Wlogical-op -Wnull-dereference -Wdouble-promotion -Wcast-align

# _POSIX_C_SOURCE and _DEFAULT_SOURCE expose the POSIX APIs the compiler uses
# (open, fstat, fdopen, strdup, isatty, mkstemp) without relying on GNU
# extensions that would not be available in a strict C17 environment.
CPPFLAGS += -Iinclude -I$(BUILD_DIR) -D_POSIX_C_SOURCE=200809L -D_DEFAULT_SOURCE

CFLAGS ?= -O0 -g
CFLAGS += $(CSTD) $(ARCH_FLAGS) $(WARN_FLAGS) -MMD -MP -fPIC

LDFLAGS ?=
LDLIBS ?=

ifeq ($(TRACEY_USE_ZLIB),1)
CPPFLAGS += -DTRACEY_HAVE_ZLIB=1
LDLIBS += -lz
endif
ifeq ($(TRACEY_USE_THREADS),1)
CPPFLAGS += -DTRACEY_HAVE_THREADS=1
LDLIBS += -lpthread
endif

CPPFLAGS += -DTRACEY_LIBDIR=\"$(LIBDIR)\"

ifeq ($(ASAN),1)
CFLAGS += -fsanitize=address -fno-omit-frame-pointer
LDFLAGS += -fsanitize=address
endif
ifeq ($(UBSAN),1)
CFLAGS += -fsanitize=undefined
LDFLAGS += -fsanitize=undefined
endif

KCONFIG_HEADER := $(BUILD_DIR)/kconfig_autoconf.h
CONFIG_H := $(BUILD_DIR)/config.h
GENERATED_HEADERS := $(KCONFIG_HEADER) $(CONFIG_H)

OBJ_DIR := $(BUILD_DIR)/obj
TRACEYC := $(BUILD_DIR)/traceyc
TEST_LEXER := $(BUILD_DIR)/tracey_lexer_test
TEST_SOURCE := $(BUILD_DIR)/tracey_source_test
TEST_ARGUMENT := $(BUILD_DIR)/tracey_argument_test
TEST_PARSER := $(BUILD_DIR)/tracey_parser_test
TEST_SEMANTIC := $(BUILD_DIR)/tracey_semantic_test
TEST_BINS := $(TEST_LEXER) $(TEST_SOURCE) $(TEST_ARGUMENT) $(TEST_PARSER) $(TEST_SEMANTIC)

# Verbose control (Linux-kernel style): V=1 shows full commands.
V ?= 0
Q := $(if $(V),,@)
