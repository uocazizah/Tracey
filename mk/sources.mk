# mk/sources.mk - Source file lists (explicit, no GLOB).
#
# Every source in this project is ISO C17; no C++ sources remain.

COMMON_SRCS       := general/common/common_string.c general/common/common_buf.c \
                     general/common/common_alloc.c
SOURCE_SRCS       := general/source/source.c general/source/source_read.c
LEXER_SRCS        := general/lexer/lexer.c general/lexer/lexer_scan.c \
                     general/lexer/lexer_token.c
ARGUMENT_SRCS     := general/argument/argument.c general/argument/argument_parse.c \
                     general/argument/argument_help.c general/argument/argument_debug.c
AST_SRCS          := general/ast/ast.c general/ast/ast_program.c \
                     general/ast/ast_destroy.c
PARSER_SRCS       := general/parser/parser.c general/parser/parser_types.c \
                     general/parser/parser_expr.c general/parser/parser_stmt.c \
                     general/parser/parser_decl.c
SEMANTIC_SRCS     := general/semantic/semantic.c general/semantic/semantic_helpers.c \
                     general/semantic/semantic_expr.c general/semantic/semantic_stmt.c
CODEGEN_SRCS      := general/codegen/codegen.c general/codegen/codegen_helpers.c \
                     general/codegen/codegen_expr.c general/codegen/codegen_stmt.c
DIAGNOSTICS_SRCS  := general/diagnostics/diagnostics.c
PREPROCESSOR_SRCS := general/preprocessor/preprocessor.c general/preprocessor/preprocessor_util.c \
                     general/preprocessor/preprocessor_macro.c general/preprocessor/preprocessor_directive.c
PIPELINE_SRCS     := general/pipeline/pipeline.c general/pipeline/pipeline_util.c \
                     general/pipeline/pipeline_preprocess.c general/pipeline/pipeline_lex.c \
                     general/pipeline/pipeline_parse.c general/pipeline/pipeline_semantic.c \
                     general/pipeline/pipeline_codegen.c
RUNTIME_SRCS      := general/runtime/runtime.c general/runtime/runtime_print.c \
                     general/runtime/runtime_input.c general/runtime/runtime_char.c \
                     general/runtime/runtime_line.c general/runtime/runtime_scanf.c \
                     general/runtime/runtime_read.c
ELF_SRCS          := general/elf/elf.c
ASSEMBLER_SRCS    := general/assembler/assembler.c general/assembler/assembler_helpers.c \
                     general/assembler/assembler_encode.c
LINKER_SRCS       := general/linker/linker.c general/linker/linker_elf.c \
                     general/linker/linker_symbols.c general/linker/linker_reloc.c \
                     general/linker/linker_output.c

ALL_MODULE_SRCS := $(COMMON_SRCS) $(SOURCE_SRCS) $(LEXER_SRCS) $(ARGUMENT_SRCS) \
                   $(AST_SRCS) $(PARSER_SRCS) $(SEMANTIC_SRCS) $(CODEGEN_SRCS) \
                   $(DIAGNOSTICS_SRCS) $(PREPROCESSOR_SRCS) $(PIPELINE_SRCS) \
                   $(RUNTIME_SRCS) $(ELF_SRCS) $(ASSEMBLER_SRCS) $(LINKER_SRCS)

TEST_SRCS := test/test_lexer.c test/test_source.c test/test_argument.c \
             test/test_parser.c test/test_semantic.c
