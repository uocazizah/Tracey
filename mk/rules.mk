# mk/rules.mk - Build rules.
#
# The project is built exclusively with a C17 compiler. Libraries are built
# as static archives and, for the public modules, as shared objects as well.

.PHONY: all clean test test-e2e menuconfig olddefconfig help \
        menuconfig-minimal menuconfig-default menuconfig-all \
        install uninstall

STATIC_LIBS := $(BUILD_DIR)/libtracey_common.a \
               $(BUILD_DIR)/libtracey_source.a \
               $(BUILD_DIR)/libtracey_lexer.a \
               $(BUILD_DIR)/libtracey_argument.a \
               $(BUILD_DIR)/libtracey_ast.a \
               $(BUILD_DIR)/libtracey_parser.a \
               $(BUILD_DIR)/libtracey_semantic.a \
               $(BUILD_DIR)/libtracey_codegen.a \
               $(BUILD_DIR)/libtracey_diagnostics.a \
               $(BUILD_DIR)/libtracey_preprocessor.a \
               $(BUILD_DIR)/libtracey_pipeline.a \
               $(BUILD_DIR)/libtracey_runtime.a \
               $(BUILD_DIR)/libtracey_elf.a \
               $(BUILD_DIR)/libtracey_assembler.a \
               $(BUILD_DIR)/libtracey_linker.a

SHARED_LIBS := $(BUILD_DIR)/libtracey_source.so \
               $(BUILD_DIR)/libtracey_lexer.so \
               $(BUILD_DIR)/libtracey_argument.so \
               $(BUILD_DIR)/libtracey_preprocessor.so \
               $(BUILD_DIR)/libtracey_pipeline.so \
               $(BUILD_DIR)/libtracey_runtime.so \
               $(BUILD_DIR)/libtracey_assembler.so \
               $(BUILD_DIR)/libtracey_linker.so

all: $(CONFIG_FILE) $(TRACEYC) $(BUILD_DIR)/traceyas $(BUILD_DIR)/traceyld \
     $(STATIC_LIBS) $(SHARED_LIBS) $(TEST_BINS)

# Bootstrap .config target (safety net; parse-time bootstrap usually creates it).
$(CONFIG_FILE):
	$(PYTHON) scripts/kconfig_tool.py olddefconfig --kconfig Kconfig --config $(CONFIG_FILE)

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

$(KCONFIG_HEADER): Kconfig $(CONFIG_FILE) scripts/kconfig_tool.py | $(BUILD_DIR)
	$(PYTHON) scripts/kconfig_tool.py gen-header --kconfig Kconfig --config $(CONFIG_FILE) --header $@

$(CONFIG_H): config.h.in | $(BUILD_DIR)
	sed 's/@PROJECT_VERSION@/$(PROJECT_VERSION)/' config.h.in > $@

# All object files, including tools, are produced from C sources by this rule.
$(OBJ_DIR)/%.o: %.c $(GENERATED_HEADERS)
	@mkdir -p $(dir $@)
	$(Q)$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/libtracey_common.a: $(patsubst %.c,$(OBJ_DIR)/%.o,$(COMMON_SRCS))
	$(Q)ar rcs $@ $^

$(BUILD_DIR)/libtracey_source.a: $(patsubst %.c,$(OBJ_DIR)/%.o,$(SOURCE_SRCS))
	$(Q)ar rcs $@ $^

$(BUILD_DIR)/libtracey_lexer.a: $(patsubst %.c,$(OBJ_DIR)/%.o,$(LEXER_SRCS))
	$(Q)ar rcs $@ $^

$(BUILD_DIR)/libtracey_argument.a: $(patsubst %.c,$(OBJ_DIR)/%.o,$(ARGUMENT_SRCS))
	$(Q)ar rcs $@ $^

$(BUILD_DIR)/libtracey_ast.a: $(patsubst %.c,$(OBJ_DIR)/%.o,$(AST_SRCS))
	$(Q)ar rcs $@ $^

$(BUILD_DIR)/libtracey_parser.a: $(patsubst %.c,$(OBJ_DIR)/%.o,$(PARSER_SRCS))
	$(Q)ar rcs $@ $^

$(BUILD_DIR)/libtracey_semantic.a: $(patsubst %.c,$(OBJ_DIR)/%.o,$(SEMANTIC_SRCS))
	$(Q)ar rcs $@ $^

$(BUILD_DIR)/libtracey_codegen.a: $(patsubst %.c,$(OBJ_DIR)/%.o,$(CODEGEN_SRCS))
	$(Q)ar rcs $@ $^

$(BUILD_DIR)/libtracey_diagnostics.a: $(patsubst %.c,$(OBJ_DIR)/%.o,$(DIAGNOSTICS_SRCS))
	$(Q)ar rcs $@ $^

$(BUILD_DIR)/libtracey_preprocessor.a: $(patsubst %.c,$(OBJ_DIR)/%.o,$(PREPROCESSOR_SRCS))
	$(Q)ar rcs $@ $^

$(BUILD_DIR)/libtracey_pipeline.a: $(patsubst %.c,$(OBJ_DIR)/%.o,$(PIPELINE_SRCS))
	$(Q)ar rcs $@ $^

$(BUILD_DIR)/libtracey_runtime.a: $(patsubst %.c,$(OBJ_DIR)/%.o,$(RUNTIME_SRCS))
	$(Q)ar rcs $@ $^

$(BUILD_DIR)/libtracey_elf.a: $(patsubst %.c,$(OBJ_DIR)/%.o,$(ELF_SRCS))
	$(Q)ar rcs $@ $^

$(BUILD_DIR)/libtracey_assembler.a: $(patsubst %.c,$(OBJ_DIR)/%.o,$(ASSEMBLER_SRCS))
	$(Q)ar rcs $@ $^

$(BUILD_DIR)/libtracey_linker.a: $(patsubst %.c,$(OBJ_DIR)/%.o,$(LINKER_SRCS))
	$(Q)ar rcs $@ $^

# Shared objects are built from the matching static archive so that the same
# PIC objects are reused. Undefined references to other modules are resolved
# when the final executable is linked, or at load time from the installed
# libraries.
$(BUILD_DIR)/libtracey_%.so: $(BUILD_DIR)/libtracey_%.a
	$(Q)$(CC) -shared -o $@ -Wl,--whole-archive $< -Wl,--no-whole-archive $(LDFLAGS)

$(BUILD_DIR)/traceyas: $(OBJ_DIR)/tools/tracey-as.o $(BUILD_DIR)/libtracey_assembler.a $(BUILD_DIR)/libtracey_elf.a $(BUILD_DIR)/libtracey_common.a
	$(Q)$(CC) $(LDFLAGS) $^ $(LDLIBS) -o $@

$(BUILD_DIR)/traceyld: $(OBJ_DIR)/tools/tracey-ld.o $(BUILD_DIR)/libtracey_linker.a $(BUILD_DIR)/libtracey_elf.a $(BUILD_DIR)/libtracey_common.a
	$(Q)$(CC) $(LDFLAGS) $^ $(LDLIBS) -o $@

# traceyc is linked statically against every module so that it runs directly
# from the build directory without a library search path.
$(TRACEYC): $(OBJ_DIR)/tools/traceyc/main.o \
            $(BUILD_DIR)/libtracey_pipeline.a \
            $(BUILD_DIR)/libtracey_codegen.a \
            $(BUILD_DIR)/libtracey_semantic.a \
            $(BUILD_DIR)/libtracey_parser.a \
            $(BUILD_DIR)/libtracey_ast.a \
            $(BUILD_DIR)/libtracey_lexer.a \
            $(BUILD_DIR)/libtracey_preprocessor.a \
            $(BUILD_DIR)/libtracey_source.a \
            $(BUILD_DIR)/libtracey_argument.a \
            $(BUILD_DIR)/libtracey_runtime.a \
            $(BUILD_DIR)/libtracey_assembler.a \
            $(BUILD_DIR)/libtracey_linker.a \
            $(BUILD_DIR)/libtracey_elf.a \
            $(BUILD_DIR)/libtracey_diagnostics.a \
            $(BUILD_DIR)/libtracey_common.a
	$(Q)$(CC) $(LDFLAGS) $^ $(LDLIBS) -o $@

$(TEST_LEXER): $(OBJ_DIR)/test/test_lexer.o $(BUILD_DIR)/libtracey_lexer.a $(BUILD_DIR)/libtracey_common.a
	$(Q)$(CC) $(LDFLAGS) $^ $(LDLIBS) -o $@

$(TEST_SOURCE): $(OBJ_DIR)/test/test_source.o $(BUILD_DIR)/libtracey_source.a $(BUILD_DIR)/libtracey_common.a
	$(Q)$(CC) $(LDFLAGS) $^ $(LDLIBS) -o $@

$(TEST_ARGUMENT): $(OBJ_DIR)/test/test_argument.o $(BUILD_DIR)/libtracey_argument.a $(BUILD_DIR)/libtracey_common.a
	$(Q)$(CC) $(LDFLAGS) $^ $(LDLIBS) -o $@

$(TEST_PARSER): $(OBJ_DIR)/test/test_parser.o $(BUILD_DIR)/libtracey_parser.a $(BUILD_DIR)/libtracey_ast.a $(BUILD_DIR)/libtracey_lexer.a $(BUILD_DIR)/libtracey_common.a
	$(Q)$(CC) $(LDFLAGS) $^ $(LDLIBS) -o $@

$(TEST_SEMANTIC): $(OBJ_DIR)/test/test_semantic.o $(BUILD_DIR)/libtracey_semantic.a $(BUILD_DIR)/libtracey_parser.a $(BUILD_DIR)/libtracey_ast.a $(BUILD_DIR)/libtracey_lexer.a $(BUILD_DIR)/libtracey_common.a
	$(Q)$(CC) $(LDFLAGS) $^ $(LDLIBS) -o $@

# End-to-end tests: tests/run.sh compiles each tests/case_*.trc with traceyc,
# executes the resulting binary, and checks the exit code and stdout against
# the expectations embedded in each case.
#
# Code generation targets AArch64 Linux and the produced binaries are executed
# directly, so these cases are only meaningful when the host can run AArch64
# code. On other hosts the e2e step is skipped rather than failing the build.
HOST_ARCH := $(shell uname -m)
ifeq ($(HOST_ARCH),aarch64)
E2E_CMD := bash tests/run.sh $(TRACEYC)
else
E2E_CMD := echo "[e2e] skipped: host '$(HOST_ARCH)' cannot execute AArch64 binaries"
endif

test: $(TEST_BINS) $(TRACEYC)
	$(Q)$(TEST_LEXER)
	$(Q)$(TEST_SOURCE)
	$(Q)$(TEST_ARGUMENT)
	$(Q)$(TEST_PARSER)
	$(Q)$(TEST_SEMANTIC)
	$(Q)$(E2E_CMD)

test-e2e: $(TRACEYC)
	$(Q)$(E2E_CMD)

menuconfig:
	$(PYTHON) scripts/kconfig_tool.py menuconfig --kconfig Kconfig --config $(CONFIG_FILE)

menuconfig-minimal:
	cp etc/minimal.config $(CONFIG_FILE)
	$(PYTHON) scripts/kconfig_tool.py olddefconfig --kconfig Kconfig --config $(CONFIG_FILE)

menuconfig-default:
	cp etc/default.config $(CONFIG_FILE)
	$(PYTHON) scripts/kconfig_tool.py olddefconfig --kconfig Kconfig --config $(CONFIG_FILE)

menuconfig-all:
	cp etc/all.config $(CONFIG_FILE)
	$(PYTHON) scripts/kconfig_tool.py olddefconfig --kconfig Kconfig --config $(CONFIG_FILE)

help:
	@echo "Targets:"
	@echo "  all          Build compiler and tools"
	@echo "  test         Run unit tests and end-to-end language tests"
	@echo "  test-e2e     Run only the end-to-end language tests"
	@echo "  menuconfig            Configure build (interactive)"
	@echo "  menuconfig-minimal    Use minimal preset (no assembler/linker)"
	@echo "  menuconfig-default    Use default preset"
	@echo "  menuconfig-all        Enable all features"
	@echo "  olddefconfig Use defaults for new options"
	@echo "  clean        Remove build directory"
	@echo ""
	@echo "Variables:"
	@echo "  CC=...       C compiler to use (default: cc)"
	@echo "  V=1          Verbose build"
	@echo "  ASAN=1       Enable AddressSanitizer"
	@echo "  UBSAN=1      Enable UBSanitizer"

olddefconfig:
	$(PYTHON) scripts/kconfig_tool.py olddefconfig --kconfig Kconfig --config $(CONFIG_FILE)

install: all
	@mkdir -p $(DESTDIR)$(BINDIR) $(DESTDIR)$(LIBDIR)
	install -m 755 $(BUILD_DIR)/traceyc $(DESTDIR)$(BINDIR)/traceyc
	install -m 755 $(BUILD_DIR)/traceyas $(DESTDIR)$(BINDIR)/traceyas
	install -m 755 $(BUILD_DIR)/traceyld $(DESTDIR)$(BINDIR)/traceyld
	install -m 644 lib/stdio.htrc $(DESTDIR)$(LIBDIR)/stdio.htrc
	install -m 644 lib/stdio.trc $(DESTDIR)$(LIBDIR)/stdio.trc
	@for lib in $(STATIC_LIBS) $(SHARED_LIBS); do \
		[ -f "$$lib" ] && install -m 644 "$$lib" $(DESTDIR)$(LIBDIR)/ || true; \
	done
	@echo "Installed to $(DESTDIR)$(PREFIX)"

uninstall:
	rm -f $(DESTDIR)$(BINDIR)/traceyc
	rm -f $(DESTDIR)$(BINDIR)/traceyas
	rm -f $(DESTDIR)$(BINDIR)/traceyld
	rm -f $(DESTDIR)$(LIBDIR)/stdio.htrc
	rm -f $(DESTDIR)$(LIBDIR)/stdio.trc
	rm -f $(DESTDIR)$(LIBDIR)/libtracey_*.a
	rm -f $(DESTDIR)$(LIBDIR)/libtracey_*.so
	-rmdir $(DESTDIR)$(LIBDIR) 2>/dev/null || true
	@echo "Uninstalled from $(DESTDIR)$(PREFIX)"

clean:
	rm -rf $(BUILD_DIR)

-include $(shell find $(OBJ_DIR) -name '*.d' 2>/dev/null)
