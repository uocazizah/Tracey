#include <doctest/doctest.h>

#include <tracey/lexer.hxx>
#include <tracey/parser.hxx>
#include <tracey/ast.hxx>

#include <string>

using namespace tracey;

static bool parse_source(const std::string& src) {
    lexer lx{src};
    parser p{lx};
    program prog;
    return p.parse(prog);
}

TEST_CASE("parser: empty program") {
    CHECK(parse_source(""));
}

TEST_CASE("parser: simple function") {
    CHECK(parse_source("int main() { return 0; }"));
}

TEST_CASE("parser: var decl and if") {
    CHECK(parse_source("int main() { int x = 1; if (x > 0) { return x; } else { return 0; } }"));
}

TEST_CASE("parser: while and call") {
    CHECK(parse_source("void f() { int i = 0; while (i < 3) { i = i + 1; } print(\"done\"); }"));
}

TEST_CASE("parser: syntax error reported") {
    CHECK_FALSE(parse_source("int main() { int x = ; }"));
}