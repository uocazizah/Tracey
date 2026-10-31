# mk/toolchain.mk - Compiler and build flags.
CXX ?= g++
# Use ccache if available for faster rebuilds
CCACHE := $(shell command -v ccache 2>/dev/null)
ifneq ($(CCACHE),)
CXX := ccache $(CXX)
endif
CXXSTD ?= -std=c++23
PROJECT_VERSION ?= 1.0.0
BUILD_DIR ?= build

WARN_FLAGS := -Wall -Wextra -Wpedantic -Wshadow -Wunused -Wconversion -Wsign-conversion -Wformat=2 -Wimplicit-fallthrough -Wmisleading-indentation -Wduplicated-cond -Wduplicated-branches -Wlogical-op -Wnull-dereference -Wdouble-promotion -Wcast-align -Wuseless-cast
CPPFLAGS := -Iinclude -I$(BUILD_DIR)
CXXFLAGS ?= -O0 -g
CXXFLAGS += $(CXXSTD) $(ARCH_FLAGS) $(WARN_FLAGS) -MMD -MP -fPIC

LDFLAGS ?=
LDLIBS :=

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
CXXFLAGS += -fsanitize=address -fno-omit-frame-pointer
LDFLAGS += -fsanitize=address
endif
ifeq ($(UBSAN),1)
CXXFLAGS += -fsanitize=undefined
LDFLAGS += -fsanitize=undefined
endif

KCONFIG_HEADER := $(BUILD_DIR)/kconfig_autoconf.h
CONFIG_H := $(BUILD_DIR)/config.h
GENERATED_HEADERS := $(KCONFIG_HEADER) $(CONFIG_H)

OBJ_DIR := $(BUILD_DIR)/obj
TRACEYC := $(BUILD_DIR)/traceyc
TEST_LEXER := $(BUILD_DIR)/tracey_lexer_test
TEST_PARSER := $(BUILD_DIR)/tracey_parser_test
TEST_SEMANTIC := $(BUILD_DIR)/tracey_semantic_test

# Verbose control (Linux-kernel style): V=1 shows full commands.
V ?= 0
Q := $(if $(V),,@)
