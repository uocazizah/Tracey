/*
 * Semantic analyzer unit tests.
 *
 * Programs are parsed from source text and then passed to the semantic
 * analyzer. The helper reports true only when both stages succeed, which
 * matches the contract of the former C++ tests: an invalid program is
 * expected to be rejected by one of the two stages and must not be
 * reported as valid.
 *
 * Every allocated resource is released on all paths: the parsed program
 * with tracey_program_free, parser diagnostics with tracey_parser_free
 * and analyzer diagnostics and scopes with tracey_semantic_free.
 */

#include "test_harness.h"

#include <tracey/lexer.h>
#include <tracey/parser.h>
#include <tracey/semantic.h>

#include <string.h>

/*
 * semantic_ok - parse and semantically analyze a NUL-terminated source.
 *
 * Returns false when parsing fails; the analyzer is not run on a partial
 * or malformed program. When parsing succeeds, the program is analyzed
 * and the analyzer result is returned. All memory owned by the parser,
 * the program and the analyzer is released before returning.
 */
static bool semantic_ok(const char *src)
{
    struct tracey_lexer lexer;
    struct tracey_parser parser;
    struct tracey_program program;
    struct tracey_semantic_analyzer analyzer;
    bool parsed;
    bool ok;

    tracey_lexer_init(&lexer, src, strlen(src));
    tracey_parser_init(&parser, &lexer);
    parsed = tracey_parser_parse(&parser, &program);
    if (!parsed)
    {
        tracey_program_free(&program);
        tracey_parser_free(&parser);
        return false;
    }

    /*
     * tracey_semantic_analyze internally resets and initializes the
     * analyzer, but the structure must still start in a safe state so
     * that the final tracey_semantic_free observes consistent fields.
     */
    tracey_semantic_init(&analyzer);
    ok = tracey_semantic_analyze(&analyzer, &program);

    tracey_semantic_free(&analyzer);
    tracey_program_free(&program);
    tracey_parser_free(&parser);
    return ok;
}

TEST_CASE(semantic_valid_program)
{
    CHECK(semantic_ok("int main() { int x = 1; return x; }"));
}

TEST_CASE(semantic_undefined_variable)
{
    CHECK_FALSE(semantic_ok("int main() { return x; }"));
}

TEST_CASE(semantic_type_mismatch)
{
    CHECK_FALSE(semantic_ok("int main() { bool b = 1; return 0; }"));
}

TEST_CASE(semantic_wrong_arity)
{
    CHECK_FALSE(semantic_ok("int f(int a) { return a; } int main() { return f(1,2); }"));
}

TEST_CASE(semantic_void_returns_value)
{
    CHECK_FALSE(semantic_ok("void f() { return 1; } int main() { return 0; }"));
}

TEST_CASE(semantic_char_pointer_dereference)
{
    CHECK(semantic_ok("int main() { char *p_name = \"Uoc Azizah\"; char c = *p_name; return 0; }"));
}

TEST_CASE(semantic_string_pointer_distinct_from_char)
{
    CHECK(semantic_ok("int main() { char *p_name = \"Uoc Azizah\"; print(p_name); print(*p_name); return 0; }"));
}

TEST_CASE(semantic_input_int_valid)
{
    CHECK(semantic_ok("int main() { int x = input_int(); return x; }"));
}

TEST_CASE(semantic_input_str_valid)
{
    CHECK(semantic_ok("int main() { char buf[16]; input_str(buf, 16); return 0; }"));
}

TEST_CASE(semantic_input_str_wrong_arity)
{
    CHECK_FALSE(semantic_ok("int main() { char buf[16]; input_str(buf); return 0; }"));
}

TEST_CASE(semantic_input_str_type_mismatch)
{
    CHECK_FALSE(semantic_ok("int main() { int x; input_str(x, 16); return 0; }"));
}

TEST_CASE(semantic_struct_member_read_write)
{
    CHECK(semantic_ok("struct P { int x; int y; }; "
                      "int main() { struct P p; p.x = 1; p.y = 2; return p.x + p.y; }"));
}

TEST_CASE(semantic_struct_nested_member)
{
    CHECK(semantic_ok("struct Inner { int a; }; struct Outer { struct Inner in; }; "
                      "int main() { struct Outer o; o.in.a = 5; return o.in.a; }"));
}

TEST_CASE(semantic_struct_unknown_member_reported)
{
    CHECK_FALSE(semantic_ok("struct P { int x; }; int main() { struct P p; return p.z; }"));
}

TEST_CASE(semantic_struct_array_valid)
{
    CHECK(semantic_ok("struct P { int x; }; int main() { struct P a[3]; a[0].x = 1; return a[0].x; }"));
}

TEST_CASE(semantic_struct_compound_assign_to_member_rejected)
{
    CHECK_FALSE(semantic_ok("struct P { int x; }; int main() { struct P p; p.x += 1; return p.x; }"));
}

TEST_CASE(semantic_member_access_on_non_struct_rejected)
{
    CHECK_FALSE(semantic_ok("int main() { int x = 1; return x.field; }"));
}

TEST_MAIN_BEGIN("semantic")
    TEST_RUN(semantic_valid_program);
    TEST_RUN(semantic_undefined_variable);
    TEST_RUN(semantic_type_mismatch);
    TEST_RUN(semantic_wrong_arity);
    TEST_RUN(semantic_void_returns_value);
    TEST_RUN(semantic_char_pointer_dereference);
    TEST_RUN(semantic_string_pointer_distinct_from_char);
    TEST_RUN(semantic_input_int_valid);
    TEST_RUN(semantic_input_str_valid);
    TEST_RUN(semantic_input_str_wrong_arity);
    TEST_RUN(semantic_input_str_type_mismatch);
    TEST_RUN(semantic_struct_member_read_write);
    TEST_RUN(semantic_struct_nested_member);
    TEST_RUN(semantic_struct_unknown_member_reported);
    TEST_RUN(semantic_struct_array_valid);
    TEST_RUN(semantic_struct_compound_assign_to_member_rejected);
    TEST_RUN(semantic_member_access_on_non_struct_rejected);
TEST_MAIN_END
