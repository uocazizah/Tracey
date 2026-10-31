#include <doctest/doctest.h>

#include <tracey/lexer.hxx>
#include <tracey/parser.hxx>
#include <tracey/semantic.hxx>

#include <string>

using namespace tracey;

static bool semantic_ok(const std::string &src)
{
    lexer lx{src};
    parser p{lx};
    program prog;
    if (!p.parse(prog))
        return false;
    semantic_analyzer sem;
    return sem.analyze(prog);
}

TEST_CASE("semantic: valid program")
{
    CHECK(semantic_ok("int main() { int x = 1; return x; }"));
}

TEST_CASE("semantic: undefined variable")
{
    CHECK_FALSE(semantic_ok("int main() { return x; }"));
}

TEST_CASE("semantic: type mismatch")
{
    CHECK_FALSE(semantic_ok("int main() { bool b = 1; return 0; }"));
}

TEST_CASE("semantic: wrong arity")
{
    CHECK_FALSE(semantic_ok("int f(int a) { return a; } int main() { return f(1,2); }"));
}

TEST_CASE("semantic: void returns value")
{
    CHECK_FALSE(semantic_ok("void f() { return 1; } int main() { return 0; }"));
}
