# mk/rules.mk - Build rules.

.PHONY: all clean test menuconfig olddefconfig help \
        menuconfig-minimal menuconfig-default menuconfig-all \
        install uninstall

all: $(CONFIG_FILE) $(TRACEYC) $(TEST_LEXER) $(BUILD_DIR)/traceyas $(BUILD_DIR)/traceyld \
     $(BUILD_DIR)/libtracey_source.so $(BUILD_DIR)/libtracey_lexer.so $(BUILD_DIR)/libtracey_argument.so \
     $(BUILD_DIR)/libtracey_assembler.so $(BUILD_DIR)/libtracey_linker.so

# Bootstrap .config target (safety net; parse-time bootstrap usually creates it).
$(CONFIG_FILE):
	python3 scripts/kconfig_tool.py olddefconfig --kconfig Kconfig --config $(CONFIG_FILE)

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

$(KCONFIG_HEADER): Kconfig $(CONFIG_FILE) scripts/kconfig_tool.py | $(BUILD_DIR)
	python3 scripts/kconfig_tool.py gen-header --kconfig Kconfig --config $(CONFIG_FILE) --header $@

$(CONFIG_H): config.h.in | $(BUILD_DIR)
	sed 's/@PROJECT_VERSION@/$(PROJECT_VERSION)/' config.h.in > $@

$(OBJ_DIR)/%.o: %.cxx $(GENERATED_HEADERS)
	@mkdir -p $(dir $@)
	$(Q)$(CXX) $(CPPFLAGS) $(CXXFLAGS) -c $< -o $@

$(BUILD_DIR)/libtracey_source.a: $(patsubst %.cxx,$(OBJ_DIR)/%.o,$(SOURCE_SRCS))
	$(Q)ar rcs $@ $^

$(BUILD_DIR)/libtracey_lexer.a: $(patsubst %.cxx,$(OBJ_DIR)/%.o,$(LEXER_SRCS))
	$(Q)ar rcs $@ $^

$(BUILD_DIR)/libtracey_argument.a: $(patsubst %.cxx,$(OBJ_DIR)/%.o,$(ARGUMENT_SRCS))
	$(Q)ar rcs $@ $^

$(OBJ_DIR)/index.o: index.cxx $(GENERATED_HEADERS)
	@mkdir -p $(dir $@)
	$(Q)$(CXX) $(CPPFLAGS) $(CXXFLAGS) -c $< -o $@

$(BUILD_DIR)/libtracey_assembler.a: $(patsubst %.cxx,$(OBJ_DIR)/%.o,$(ASSEMBLER_SRCS))
	$(Q)ar rcs $@ $^

$(BUILD_DIR)/libtracey_linker.a: $(patsubst %.cxx,$(OBJ_DIR)/%.o,$(LINKER_SRCS)) $(patsubst %.cxx,$(OBJ_DIR)/%.o,$(ELF_SRCS))
	$(Q)ar rcs $@ $^

$(BUILD_DIR)/libtracey_source.so: $(patsubst %.cxx,$(OBJ_DIR)/%.o,$(SOURCE_SRCS))
	$(Q)$(CXX) -shared -o $@ $^

$(BUILD_DIR)/libtracey_lexer.so: $(patsubst %.cxx,$(OBJ_DIR)/%.o,$(LEXER_SRCS))
	$(Q)$(CXX) -shared -o $@ $^

$(BUILD_DIR)/libtracey_argument.so: $(patsubst %.cxx,$(OBJ_DIR)/%.o,$(ARGUMENT_SRCS))
	$(Q)$(CXX) -shared -o $@ $^

$(BUILD_DIR)/libtracey_assembler.so: $(BUILD_DIR)/libtracey_assembler.a
	$(Q)$(CXX) -shared -o $@ -Wl,--whole-archive $^ -Wl,--no-whole-archive

$(BUILD_DIR)/libtracey_linker.so: $(BUILD_DIR)/libtracey_linker.a
	$(Q)$(CXX) -shared -o $@ -Wl,--whole-archive $^ -Wl,--no-whole-archive

$(BUILD_DIR)/traceyas: tools/tracey-as.cxx $(BUILD_DIR)/libtracey_assembler.a
	$(Q)$(CXX) $(CPPFLAGS) $(CXXFLAGS) tools/tracey-as.cxx -L$(BUILD_DIR) -ltracey_assembler -Wl,-rpath,'$$ORIGIN' -Wl,-rpath,'$(LIBDIR)' -o $@

$(BUILD_DIR)/traceyld: tools/tracey-ld.cxx $(BUILD_DIR)/libtracey_linker.a
	$(Q)$(CXX) $(CPPFLAGS) $(CXXFLAGS) tools/tracey-ld.cxx -L$(BUILD_DIR) -ltracey_linker -Wl,-rpath,'$$ORIGIN' -Wl,-rpath,'$(LIBDIR)' -o $@

$(TRACEYC): $(OBJ_DIR)/index.o $(BUILD_DIR)/libtracey_source.a $(BUILD_DIR)/libtracey_lexer.a $(BUILD_DIR)/libtracey_argument.a $(BUILD_DIR)/libtracey_assembler.a $(BUILD_DIR)/libtracey_linker.a $(OBJ_DIR)/general/parser/parser.o $(OBJ_DIR)/general/parser/parser_debug.o $(OBJ_DIR)/general/semantic/semantic.o $(OBJ_DIR)/general/codegen/codegen.o $(OBJ_DIR)/general/diagnostics/diagnostics.o
	$(Q)$(CXX) $(LDFLAGS) $(OBJ_DIR)/index.o $(OBJ_DIR)/general/parser/parser.o $(OBJ_DIR)/general/parser/parser_debug.o $(OBJ_DIR)/general/semantic/semantic.o $(OBJ_DIR)/general/codegen/codegen.o $(OBJ_DIR)/general/diagnostics/diagnostics.o -L$(BUILD_DIR) -ltracey_source -ltracey_lexer -ltracey_argument -ltracey_assembler -ltracey_linker $(LDLIBS) -Wl,-rpath,'$$ORIGIN' -Wl,-rpath,'$(LIBDIR)' -o $@

$(OBJ_DIR)/doctest_main.o: test/doctest_main.cxx $(GENERATED_HEADERS)
	@mkdir -p $(dir $@)
	$(Q)$(CXX) $(CPPFLAGS) $(CXXFLAGS) -I dependencies -c $< -o $@

$(OBJ_DIR)/test_lexer.o: test/test_lexer.cxx $(GENERATED_HEADERS)
	@mkdir -p $(dir $@)
	$(Q)$(CXX) $(CPPFLAGS) $(CXXFLAGS) -I dependencies -c $< -o $@

$(OBJ_DIR)/test_source.o: test/test_source.cxx $(GENERATED_HEADERS)
	@mkdir -p $(dir $@)
	$(Q)$(CXX) $(CPPFLAGS) $(CXXFLAGS) -I dependencies -c $< -o $@

$(OBJ_DIR)/test_argument.o: test/test_argument.cxx $(GENERATED_HEADERS)
	@mkdir -p $(dir $@)
	$(Q)$(CXX) $(CPPFLAGS) $(CXXFLAGS) -I dependencies -c $< -o $@

$(OBJ_DIR)/test_parser.o: test/test_parser.cxx $(GENERATED_HEADERS)
	@mkdir -p $(dir $@)
	$(Q)$(CXX) $(CPPFLAGS) $(CXXFLAGS) -I dependencies -c $< -o $@

$(OBJ_DIR)/test_semantic.o: test/test_semantic.cxx $(GENERATED_HEADERS)
	@mkdir -p $(dir $@)
	$(Q)$(CXX) $(CPPFLAGS) $(CXXFLAGS) -I dependencies -c $< -o $@

$(TEST_LEXER): $(OBJ_DIR)/doctest_main.o $(OBJ_DIR)/test_lexer.o $(OBJ_DIR)/test_source.o $(OBJ_DIR)/test_argument.o $(BUILD_DIR)/libtracey_lexer.a $(BUILD_DIR)/libtracey_source.a $(BUILD_DIR)/libtracey_argument.a
	$(Q)$(CXX) $(LDFLAGS) $(OBJ_DIR)/doctest_main.o $(OBJ_DIR)/test_lexer.o $(OBJ_DIR)/test_source.o $(OBJ_DIR)/test_argument.o -L$(BUILD_DIR) -ltracey_lexer -ltracey_source -ltracey_argument $(LDLIBS) -o $@

$(TEST_PARSER): $(OBJ_DIR)/doctest_main.o $(OBJ_DIR)/test_parser.o $(OBJ_DIR)/general/parser/parser.o $(OBJ_DIR)/general/parser/parser_debug.o $(BUILD_DIR)/libtracey_lexer.a $(BUILD_DIR)/libtracey_source.a $(BUILD_DIR)/libtracey_argument.a
	$(Q)$(CXX) $(LDFLAGS) $(OBJ_DIR)/doctest_main.o $(OBJ_DIR)/test_parser.o $(OBJ_DIR)/general/parser/parser.o $(OBJ_DIR)/general/parser/parser_debug.o -L$(BUILD_DIR) -ltracey_lexer -ltracey_source -ltracey_argument $(LDLIBS) -o $@

$(TEST_SEMANTIC): $(OBJ_DIR)/doctest_main.o $(OBJ_DIR)/test_semantic.o $(OBJ_DIR)/general/parser/parser.o $(OBJ_DIR)/general/parser/parser_debug.o $(OBJ_DIR)/general/semantic/semantic.o $(BUILD_DIR)/libtracey_lexer.a $(BUILD_DIR)/libtracey_source.a $(BUILD_DIR)/libtracey_argument.a
	$(Q)$(CXX) $(LDFLAGS) $(OBJ_DIR)/doctest_main.o $(OBJ_DIR)/test_semantic.o $(OBJ_DIR)/general/parser/parser.o $(OBJ_DIR)/general/parser/parser_debug.o $(OBJ_DIR)/general/semantic/semantic.o -L$(BUILD_DIR) -ltracey_lexer -ltracey_source -ltracey_argument $(LDLIBS) -o $@

test: $(TEST_LEXER) $(TEST_PARSER) $(TEST_SEMANTIC)
	LD_LIBRARY_PATH=$(BUILD_DIR) $(TEST_LEXER)
	LD_LIBRARY_PATH=$(BUILD_DIR) $(TEST_PARSER)
	LD_LIBRARY_PATH=$(BUILD_DIR) $(TEST_SEMANTIC)

menuconfig:
	python3 scripts/kconfig_tool.py menuconfig --kconfig Kconfig --config $(CONFIG_FILE)

menuconfig-minimal:
	cp etc/minimal.config $(CONFIG_FILE)
	python3 scripts/kconfig_tool.py olddefconfig --kconfig Kconfig --config $(CONFIG_FILE)

menuconfig-default:
	cp etc/default.config $(CONFIG_FILE)
	python3 scripts/kconfig_tool.py olddefconfig --kconfig Kconfig --config $(CONFIG_FILE)

menuconfig-all:
	cp etc/all.config $(CONFIG_FILE)
	python3 scripts/kconfig_tool.py olddefconfig --kconfig Kconfig --config $(CONFIG_FILE)

help:
	@echo "Targets:"
	@echo "  all          Build compiler and tools"
	@echo "  test         Run unit tests"
	@echo "  menuconfig            Configure build (interactive)"
	@echo "  menuconfig-minimal    Use minimal preset (no assembler/linker)"
	@echo "  menuconfig-default    Use default preset"
	@echo "  menuconfig-all        Enable all features"
	@echo "  olddefconfig Use defaults for new options"
	@echo "  clean        Remove build directory"
	@echo ""
	@echo "Variables:"
	@echo "  V=1          Verbose build"
	@echo "  ASAN=1       Enable AddressSanitizer"
	@echo "  UBSAN=1      Enable UBSanitizer"

olddefconfig:
	python3 scripts/kconfig_tool.py olddefconfig --kconfig Kconfig --config $(CONFIG_FILE)

install: all
	@mkdir -p $(DESTDIR)$(BINDIR) $(DESTDIR)$(LIBDIR)
	install -m 755 $(BUILD_DIR)/traceyc $(DESTDIR)$(BINDIR)/traceyc
	install -m 755 $(BUILD_DIR)/traceyas $(DESTDIR)$(BINDIR)/traceyas
	install -m 755 $(BUILD_DIR)/traceyld $(DESTDIR)$(BINDIR)/traceyld
	install -m 644 lib/stdio.htrc $(DESTDIR)$(LIBDIR)/stdio.htrc
	install -m 644 lib/stdio.trc $(DESTDIR)$(LIBDIR)/stdio.trc
	@for lib in $(BUILD_DIR)/libtracey_*.a $(BUILD_DIR)/libtracey_*.so; do \
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

-include $(wildcard $(OBJ_DIR)/*.d) $(wildcard $(OBJ_DIR)/**/*.d)
