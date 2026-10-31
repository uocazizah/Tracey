# mk/sources.mk - Source file lists (explicit, no GLOB).
SOURCE_SRCS     := general/source/source.cxx general/source/source_debug.cxx
LEXER_SRCS      := general/lexer/lexer.cxx general/lexer/lexer_debug.cxx
ARGUMENT_SRCS   := general/argument/argument.cxx general/argument/argument_parse.cxx general/argument/argument_help.cxx general/argument/argument_debug.cxx
PARSER_SRCS     := general/parser/parser.cxx general/parser/parser_debug.cxx
ASSEMBLER_SRCS  := general/assembler/assembler.cxx
LINKER_SRCS     := general/linker/linker.cxx
ELF_SRCS        := general/elf/elf.cxx
SEMANTIC_SRCS   := general/semantic/semantic.cxx
CODEGEN_SRCS    := general/codegen/codegen.cxx
DIAGNOSTICS_SRCS:= general/diagnostics/diagnostics.cxx

ALL_MODULE_SRCS := $(SOURCE_SRCS) $(LEXER_SRCS) $(ARGUMENT_SRCS) $(PARSER_SRCS) $(SEMANTIC_SRCS) $(CODEGEN_SRCS) $(DIAGNOSTICS_SRCS)

TEST_SRCS := test/doctest_main.cxx test/test_lexer.cxx test/test_source.cxx test/test_argument.cxx test/test_parser.cxx test/test_semantic.cxx
