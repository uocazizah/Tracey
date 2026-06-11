CC      := cc
CFLAGS  := -Wall -Wextra -Iinclude

TARGET  := bin/traceyc

SRC := index.c $(shell find general -name '*.c')

OBJ := $(patsubst %.c,bin/object/%.o,$(SRC))

all: $(TARGET)

$(TARGET): $(OBJ)
	@mkdir -p bin
	@echo LD $@
	@$(CC) $^ -o $@

bin/object/%.o: %.c
	@mkdir -p $(dir $@)
	@echo CC $<
	@$(CC) $(CFLAGS) -c $< -o $@

clean:
	@rm -rf bin/object
	@rm -f $(TARGET)

.PHONY: all clean
