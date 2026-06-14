CC ?= gcc

ARCH ?= native

BASE_CFLAGS := \
	-Wall \
	-Wextra \
	-std=c11 \
	-Iinclude \
	-g \
	-O0

CFLAGS += $(BASE_CFLAGS)
LDFLAGS +=

ifeq ($(ARCH),i686)
	CFLAGS += -m32
	LDFLAGS += -m32
endif

ifeq ($(ARCH),x86_64)
	CFLAGS += -m64
	LDFLAGS += -m64
endif

TARGET := bin/traceyc

SRC := index.c $(shell find general -name '*.c')

OBJ := $(patsubst %.c,bin/object/%.o,$(SRC))

all: $(TARGET)

$(TARGET): $(OBJ)
	@mkdir -p bin
	@echo LD $@
	@$(CC) $(LDFLAGS) $^ -o $@

bin/object/%.o: %.c
	@mkdir -p $(dir $@)
	@echo CC $<
	@$(CC) $(CFLAGS) -c $< -o $@

clean:
	@rm -rf bin/object
	@rm -f $(TARGET)

info:
	@echo "CC      = $(CC)"
	@echo "ARCH    = $(ARCH)"
	@echo "CFLAGS  = $(CFLAGS)"
	@echo "LDFLAGS = $(LDFLAGS)"

.PHONY: all clean info
