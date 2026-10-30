# mk/config.mk - Configuration detection and feature flags.
#
# Bootstraps .config on the first build if it is missing, so that `make` works
# out of the box without a prior `make menuconfig`.

CONFIG_FILE ?= .config

# Only bootstrap when the requested goals actually need a build. This keeps
# `make clean`, `make help`, and the menuconfig targets from creating .config.
BUILD_GOALS := $(if $(MAKECMDGOALS),$(filter-out clean help menuconfig% olddefconfig,$(MAKECMDGOALS)),all)

ifneq ($(BUILD_GOALS),)
ifeq ($(wildcard $(CONFIG_FILE)),)
  $(info .config not found; generating default configuration...)
  _BOOTSTRAP := $(shell python3 scripts/kconfig_tool.py olddefconfig --kconfig Kconfig --config $(CONFIG_FILE))
endif
endif

HAS_CONFIG := $(wildcard $(CONFIG_FILE))

TRACEY_USE_ZLIB := $(if $(HAS_CONFIG),$(shell grep -q '^CONFIG_TRACEY_USE_ZLIB=y' $(CONFIG_FILE) 2>/dev/null && echo 1 || echo 0),0)
TRACEY_USE_THREADS := $(if $(HAS_CONFIG),$(shell grep -q '^CONFIG_TRACEY_USE_THREADS=y' $(CONFIG_FILE) 2>/dev/null && echo 1 || echo 0),0)

# Single, unified resolution of the target architecture. Environment (ARCH=...)
# always wins; otherwise derive from .config, falling back to uname -m.
ARCH ?= $(shell \
  if [ -f "$(CONFIG_FILE)" ]; then \
    if grep -q '^CONFIG_TRACEY_DEFAULT_TARGET_I686=y' "$(CONFIG_FILE)"; then echo i686; \
    elif grep -q '^CONFIG_TRACEY_DEFAULT_TARGET_X86_64=y' "$(CONFIG_FILE)"; then echo x86_64; \
    else uname -m; fi; \
  else uname -m; fi)

ARCH_FLAGS :=
ifeq ($(ARCH),i686)
ARCH_FLAGS := -m32
endif
ifeq ($(ARCH),x86_64)
ARCH_FLAGS := -m64
endif

# Sanitizers (opt-in via ASAN=1 / UBSAN=1).
ASAN ?= 0
UBSAN ?= 0

# Installation paths (GNU conventions).
PREFIX ?= /usr/local
BINDIR ?= $(PREFIX)/bin
LIBDIR ?= $(PREFIX)/lib/tracey
DESTDIR ?=