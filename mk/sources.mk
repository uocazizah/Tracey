# mk/sources.mk - Source file lists (explicit, no GLOB).
#
# Every source in this project is ISO C17; no C++ sources remain.

COMMON_SRCS       := general/common.c
SOURCE_SRCS       := general/source/source.c
LEXER_SRCS        := general/lexer/lexer.c
ARGUMENT_SRCS     := general/argument/argument.c general/argument/argument_parse.c \
                     general/argument/argument_help.c general/argument/argument_debug.c
AST_SRCS          := general/ast/ast.c
PARSER_SRCS       := general/parser/parser.c
SEMANTIC_SRCS     := general/semantic/semantic.c
CODEGEN_SRCS      := general/codegen/codegen.c
DIAGNOSTICS_SRCS  := general/diagnostics/diagnostics.c
PREPROCESSOR_SRCS := general/preprocessor/preprocessor.c
PIPELINE_SRCS     := general/pipeline/pipeline.c
RUNTIME_SRCS      := general/runtime/runtime.c
ELF_SRCS          := general/elf/elf.c
ASSEMBLER_SRCS    := general/assembler/assembler.c
LINKER_SRCS       := general/linker/linker.c

ALL_MODULE_SRCS := $(COMMON_SRCS) $(SOURCE_SRCS) $(LEXER_SRCS) $(ARGUMENT_SRCS) \
                   $(AST_SRCS) $(PARSER_SRCS) $(SEMANTIC_SRCS) $(CODEGEN_SRCS) \
                   $(DIAGNOSTICS_SRCS) $(PREPROCESSOR_SRCS) $(PIPELINE_SRCS) \
                   $(RUNTIME_SRCS) $(ELF_SRCS) $(ASSEMBLER_SRCS) $(LINKER_SRCS)

TEST_SRCS := test/test_lexer.c test/test_source.c test/test_argument.c \
             test/test_parser.c test/test_semantic.c
