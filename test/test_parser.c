/*
 * Parser unit tests.
 *
 * Each case parses a small source program through the public lexer and
 * parser interfaces and asserts only whether parsing succeeded. The
 * parser builds a heap-allocated program representation, which is always
 * released with tracey_program_free even when parsing fails, because
 * tracey_parser_parse may leave a partial program in the output.
 *
 * Parser diagnostics are owned by the parser and released with
 * tracey_parser_free. The source text is a string literal that outlives
 * the lexer and all tokens produced from it.
 */

#include "test_harness.h"

#include <tracey/lexer.h>
#include <tracey/parser.h>

#include <string.h>

/*
 * parse_source - lex and parse a NUL-terminated source string.
 *
 * Returns true only when the parser reports no diagnostics. All memory
 * created during the attempt is released before returning; diagnostics
 * are intentionally discarded because these cases assert success or
 * failure rather than specific error messages.
 */
static bool parse_source(const char *src)
{
    struct tracey_lexer lexer;
    struct tracey_parser parser;
    struct tracey_program program;
    bool ok;

    tracey_lexer_init(&lexer, src, strlen(src));
    tracey_parser_init(&parser, &lexer);
    ok = tracey_parser_parse(&parser, &program);

    tracey_program_free(&program);
    tracey_parser_free(&parser);
    return ok;
}

TEST_CASE(parser_empty_program)
{
    CHECK(parse_source(""));
}

TEST_CASE(parser_simple_function)
{
    CHECK(parse_source("int main() { return 0; }"));
}

TEST_CASE(parser_var_decl_and_if)
{
    CHECK(parse_source("int main() { int x = 1; if (x > 0) { return x; } else { return 0; } }"));
}

TEST_CASE(parser_while_and_call)
{
    CHECK(parse_source("void f() { int i = 0; while (i < 3) { i = i + 1; } print(\"done\"); }"));
}

TEST_CASE(parser_syntax_error_reported)
{
    CHECK_FALSE(parse_source("int main() { int x = ; }"));
}

TEST_CASE(parser_struct_definition_and_member_access)
{
    CHECK(parse_source("struct Point { int x; int y; }; "
                       "int main() { struct Point p; p.x = 1; p.y = 2; return p.x + p.y; }"));
}

TEST_CASE(parser_struct_nested_member_access)
{
    CHECK(parse_source("struct Inner { int a; }; struct Outer { struct Inner in; }; "
                       "int main() { struct Outer o; o.in.a = 5; return o.in.a; }"));
}

TEST_CASE(parser_unknown_struct_tag_reported)
{
    CHECK_FALSE(parse_source("int main() { struct Nope p; return 0; }"));
}

TEST_CASE(parser_shift_expressions)
{
    CHECK(parse_source("int main() { int x = 1 << 4; int y = 256 >> 3; return x + y; }"));
}

TEST_CASE(parser_hex_and_binary_literals)
{
    CHECK(parse_source("int main() { int x = 0xFF; int y = 0b1011; int a[0x4]; return x + y; }"));
}

TEST_CASE(parser_bitwise_not)
{
    CHECK(parse_source("int main() { return ~0 & 255; }"));
}

TEST_MAIN_BEGIN("parser")
    TEST_RUN(parser_empty_program);
    TEST_RUN(parser_simple_function);
    TEST_RUN(parser_var_decl_and_if);
    TEST_RUN(parser_while_and_call);
    TEST_RUN(parser_syntax_error_reported);
    TEST_RUN(parser_struct_definition_and_member_access);
    TEST_RUN(parser_struct_nested_member_access);
    TEST_RUN(parser_unknown_struct_tag_reported);
    TEST_RUN(parser_shift_expressions);
    TEST_RUN(parser_hex_and_binary_literals);
    TEST_RUN(parser_bitwise_not);
TEST_MAIN_END
