#include <doctest/doctest.h>

#include <tracey/lexer.hxx>

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace
{

using tracey::token;
using tracey::token_error;
using tracey::token_type;

/* Lex `src` completely (errors included) through the EOF token. */
std::vector<token> lex_all(std::string_view src)
{
    tracey::lexer lx{src};
    std::vector<token> tokens;
    tokens.reserve(src.size() + 1U);

    for (;;)
    {
        const token t = lx.next();
        tokens.push_back(t);
        if (t.type == token_type::eof)
            break;

        /* Every non-EOF token consumes at least one byte, so the stream can
         * never be longer than the input; guard the test itself against a
         * runaway lexer instead of hanging. */
        REQUIRE(tokens.size() <= src.size() + 1U);
    }

    return tokens;
}

/* Compact "TYPE TYPE TYPE" rendering of a token stream, used for whole
 * program-shape assertions. */
std::string dump_types(std::string_view src)
{
    std::string out;
    for (const token &t : lex_all(src))
    {
        if (!out.empty())
            out += ' ';
        out += tracey::to_string(t.type);
    }
    return out;
}

} // namespace

namespace doctest
{

/* Teach doctest how to print the lexer enums in failure messages. */
template <> struct StringMaker<tracey::token_type>
{
    static doctest::String convert(const tracey::token_type &type)
    {
        const std::string_view s = tracey::to_string(type);
        return doctest::String(s.data(), static_cast<unsigned>(s.size()));
    }
};

template <> struct StringMaker<tracey::token_error>
{
    static doctest::String convert(const tracey::token_error &error)
    {
        const std::string_view s = tracey::to_string(error);
        return doctest::String(s.data(), static_cast<unsigned>(s.size()));
    }
};

} // namespace doctest

TEST_CASE("basic program tokenizes")
{
    CHECK(dump_types("int main() {\n    return 0;\n}") ==
          "KEYWORD_INT IDENTIFIER LPAREN RPAREN LBRACE "
          "KEYWORD_RETURN INTEGER SEMICOLON RBRACE EOF");
}

TEST_CASE("line and column tracking")
{
    const auto tokens = lex_all("ab\n  cd\r\nef");

    REQUIRE(tokens.size() == 4U);
    CHECK(tokens[0].lexeme == "ab");
    CHECK(tokens[0].line == 1U);
    CHECK(tokens[0].column == 1U);
    CHECK(tokens[1].lexeme == "cd");
    CHECK(tokens[1].line == 2U);
    CHECK(tokens[1].column == 3U);
    CHECK(tokens[2].lexeme == "ef");
    CHECK(tokens[2].line == 3U);
    CHECK(tokens[2].column == 1U);
    CHECK(tokens[3].type == token_type::eof);
    CHECK(tokens[3].line == 3U);
    CHECK(tokens[3].column == 3U);
}

TEST_CASE("tabs count as one column")
{
    const auto tokens = lex_all("\tx");
    REQUIRE(tokens.size() == 2U);
    CHECK(tokens[0].column == 2U);
}

TEST_CASE("empty and whitespace-only input")
{
    CHECK(dump_types("") == "EOF");
    CHECK(dump_types("   \t\r\n  ") == "EOF");

    const auto tokens = lex_all("");
    CHECK(tokens[0].line == 1U);
    CHECK(tokens[0].column == 1U);
}

TEST_CASE("vertical tab and form feed are whitespace")
{
    CHECK(dump_types("x\vy") == "IDENTIFIER IDENTIFIER EOF");
    CHECK(dump_types("x\fy") == "IDENTIFIER IDENTIFIER EOF");

    const auto tokens = lex_all("x\vy");
    REQUIRE(tokens.size() == 3U);
    CHECK(tokens[1].column == 3U);
}

TEST_CASE("keywords")
{
    CHECK(dump_types("int return void bool true false if else while") ==
          "KEYWORD_INT KEYWORD_RETURN KEYWORD_VOID KEYWORD_BOOL "
          "KEYWORD_TRUE KEYWORD_FALSE KEYWORD_IF KEYWORD_ELSE "
          "KEYWORD_WHILE EOF");

    /* Only exact matches are keywords. */
    CHECK(dump_types("intx _int int_ return0 while1") ==
          "IDENTIFIER IDENTIFIER IDENTIFIER IDENTIFIER IDENTIFIER EOF");
}

TEST_CASE("identifiers")
{
    CHECK(dump_types("abc _x1 A9_z") == "IDENTIFIER IDENTIFIER IDENTIFIER EOF");

    const auto tokens = lex_all("abc");
    REQUIRE(tokens.size() == 2U);
    CHECK(tokens[0].lexeme == "abc");
}

TEST_CASE("integers")
{
    CHECK(dump_types("0 42 007") == "INTEGER INTEGER INTEGER EOF");

    const auto tokens = lex_all("007");
    CHECK(tokens[0].lexeme == "007");
}

TEST_CASE("integer with letter suffix is an error")
{
    CHECK(dump_types("123abc") == "ERROR EOF");

    const auto tokens = lex_all("123abc");
    REQUIRE(tokens.size() == 2U);
    CHECK(tokens[0].error == token_error::invalid_number_suffix);
    CHECK(tokens[0].lexeme == "123abc");
    CHECK(tokens[0].line == 1U);
    CHECK(tokens[0].column == 1U);
}

TEST_CASE("hex literals are rejected, not silently split")
{
    const auto tokens = lex_all("0x10");
    REQUIRE(tokens.size() == 2U);
    CHECK(tokens[0].error == token_error::invalid_number_suffix);
    CHECK(tokens[0].lexeme == "0x10");
}

TEST_CASE("delimiters")
{
    CHECK(dump_types("(){}[];,.") == "LPAREN RPAREN LBRACE RBRACE LBRACKET RBRACKET SEMICOLON "
                                     "COMMA DOT EOF");
}

TEST_CASE("operators")
{
    CHECK(dump_types("= + - * / % ! < > == != <= >= && ||") ==
          "ASSIGN PLUS MINUS STAR SLASH PERCENT BANG LESS GREATER "
          "EQUAL_EQUAL BANG_EQUAL LESS_EQUAL GREATER_EQUAL "
          "AMP_AMP PIPE_PIPE EOF");

    /* No whitespace needed around two-character operators. */
    CHECK(dump_types("a==b") == "IDENTIFIER EQUAL_EQUAL IDENTIFIER EOF");
    CHECK(dump_types("a!=b") == "IDENTIFIER BANG_EQUAL IDENTIFIER EOF");
    CHECK(dump_types("a<=b") == "IDENTIFIER LESS_EQUAL IDENTIFIER EOF");
    CHECK(dump_types("a>=b") == "IDENTIFIER GREATER_EQUAL IDENTIFIER EOF");
    CHECK(dump_types("a&&b") == "IDENTIFIER AMP_AMP IDENTIFIER EOF");
    CHECK(dump_types("a||b") == "IDENTIFIER PIPE_PIPE IDENTIFIER EOF");
}

TEST_CASE("lone pipe is an error; ampersand is address-of")
{
    CHECK(dump_types("&") == "AMPERSAND EOF");
    CHECK(dump_types("|") == "ERROR EOF");
    CHECK(dump_types("&&&") == "AMP_AMP AMPERSAND EOF");
    CHECK(dump_types("&x") == "AMPERSAND IDENTIFIER EOF");

    const auto tokens = lex_all("a & b");
    REQUIRE(tokens.size() == 4U);
    CHECK(tokens[1].type == token_type::ampersand);
    CHECK(tokens[1].lexeme == "&");
    CHECK(tokens[1].column == 3U);
}

TEST_CASE("line comments")
{
    CHECK(dump_types("x // comment\ny") == "IDENTIFIER IDENTIFIER EOF");
    CHECK(dump_types("// just a comment") == "EOF");
    CHECK(dump_types("x//comment") == "IDENTIFIER EOF");

    const auto tokens = lex_all("x // comment\ny");
    REQUIRE(tokens.size() == 3U);
    CHECK(tokens[1].line == 2U);
    CHECK(tokens[1].column == 1U);
}

TEST_CASE("block comments")
{
    CHECK(dump_types("x /* one\ntwo */ y") == "IDENTIFIER IDENTIFIER EOF");
    CHECK(dump_types("/**/") == "EOF");
    CHECK(dump_types("/* two /* is not nested */ x") == "IDENTIFIER EOF");

    /* The token after a multi-line comment has correct line/column. */
    const auto tokens = lex_all("x /* one\ntwo */ y");
    REQUIRE(tokens.size() == 3U);
    CHECK(tokens[1].line == 2U);
    CHECK(tokens[1].column == 8U);

    /* A comment can stand in for the whitespace around operators. */
    CHECK(dump_types("a/* c */+/* c */b") == "IDENTIFIER PLUS IDENTIFIER EOF");
}

TEST_CASE("unterminated block comment")
{
    CHECK(dump_types("/* nope") == "ERROR EOF");
    CHECK(dump_types("x /* nope") == "IDENTIFIER ERROR EOF");

    const auto tokens = lex_all("x\n  /* nope");
    REQUIRE(tokens.size() == 3U);
    CHECK(tokens[1].error == token_error::unterminated_comment);
    CHECK(tokens[1].line == 2U);
    CHECK(tokens[1].column == 3U);
}

TEST_CASE("slash without comment is division")
{
    CHECK(dump_types("a / b") == "IDENTIFIER SLASH IDENTIFIER EOF");
    CHECK(dump_types("a/b") == "IDENTIFIER SLASH IDENTIFIER EOF");
}

TEST_CASE("string literals")
{
    CHECK(dump_types("\"hello\"") == "STRING EOF");
    CHECK(dump_types("\"\"") == "STRING EOF");

    const auto tokens = lex_all("\"hello\"");
    REQUIRE(tokens.size() == 2U);
    CHECK(tokens[0].lexeme == "\"hello\"");
}

TEST_CASE("string escapes")
{
    CHECK(dump_types("\"a\\nb\"") == "STRING EOF");
    CHECK(dump_types("\"a\\tb\"") == "STRING EOF");
    CHECK(dump_types("\"a\\rb\"") == "STRING EOF");
    CHECK(dump_types("\"a\\0b\"") == "STRING EOF");
    CHECK(dump_types("\"a\\\\b\"") == "STRING EOF");
    CHECK(dump_types("\"a\\\"b\"") == "STRING EOF");
}

TEST_CASE("invalid string escape")
{
    CHECK(dump_types("\"a\\qb\"") == "ERROR EOF");

    const auto tokens = lex_all("\"a\\qb\"");
    REQUIRE(tokens.size() == 2U);
    CHECK(tokens[0].error == token_error::invalid_escape);
    CHECK(tokens[0].lexeme == "\\q");
    CHECK(tokens[0].line == 1U);
    CHECK(tokens[0].column == 3U);

    /* The string is consumed to its closing quote, so lexing continues. */
    CHECK(dump_types("\"a\\qb\" x") == "ERROR IDENTIFIER EOF");
}

TEST_CASE("unterminated string")
{
    CHECK(dump_types("\"abc") == "ERROR EOF");

    const auto tokens = lex_all("\"abc");
    REQUIRE(tokens.size() == 2U);
    CHECK(tokens[0].error == token_error::unterminated_string);
    CHECK(tokens[0].column == 1U);

    /* Strings do not span lines: the next line still lexes normally. */
    CHECK(dump_types("\"abc\nx") == "ERROR IDENTIFIER EOF");

    const auto after = lex_all("\"abc\nx");
    REQUIRE(after.size() == 3U);
    CHECK(after[1].line == 2U);
    CHECK(after[1].column == 1U);
}

TEST_CASE("recovery continues after errors")
{
    CHECK(dump_types("a @ b # c") == "IDENTIFIER ERROR IDENTIFIER ERROR IDENTIFIER EOF");

    const auto tokens = lex_all("a @ b # c");
    REQUIRE(tokens.size() == 6U);
    CHECK(tokens[1].error == token_error::invalid_character);
    CHECK(tokens[1].lexeme == "@");
    CHECK(tokens[1].column == 3U);
    CHECK(tokens[3].error == token_error::invalid_character);
    CHECK(tokens[3].lexeme == "#");
    CHECK(tokens[3].column == 7U);
}

TEST_CASE("token validity helpers")
{
    const auto tokens = lex_all("@");
    REQUIRE(tokens.size() == 2U);
    CHECK(tokens[0].is_error());
    CHECK_FALSE(tokens[0].valid());
    CHECK(tokens[1].valid());
    CHECK_FALSE(tokens[1].is_error());
}

TEST_CASE("peek does not consume")
{
    tracey::lexer lx{"a b"};

    const auto p1 = lx.peek();
    const auto p2 = lx.peek();
    CHECK(p1.type == token_type::identifier);
    CHECK(p2.type == token_type::identifier);
    CHECK(p1.lexeme == "a");
    CHECK(p2.lexeme == "a");

    const auto n1 = lx.next();
    CHECK(n1.type == token_type::identifier);
    CHECK(n1.lexeme == "a");

    const auto p3 = lx.peek();
    CHECK(p3.lexeme == "b");

    const auto n2 = lx.next();
    CHECK(n2.lexeme == "b");

    const auto n3 = lx.next();
    CHECK(n3.type == token_type::eof);
    CHECK(lx.is_at_end());
}

TEST_CASE("fuzz-lite: arbitrary bytes never crash, hang, or escape the source")
{
    /* Deterministic LCG: same byte stream on every platform and run. */
    std::uint64_t state = 0x0DDB1A5E5BAD5EEDULL;
    const auto step = [&state]()
    {
        state = state * 6364136223846793005ULL + 1442695040888963407ULL;
        return state;
    };

    for (int round = 0; round < 200; ++round)
    {
        const std::size_t len = (step() >> 17U) % 128U;

        std::string src;
        src.reserve(len);
        for (std::size_t i = 0; i < len; ++i)
        {
            src.push_back(static_cast<char>((step() >> 33U) & 0xFFU));
        }

        std::size_t produced = 0;
        tracey::lexer lx{src};

        for (;;)
        {
            const token t = lx.next();
            ++produced;

            if (t.type == token_type::eof)
                break;

            /* Every lexeme must be a view inside the source buffer. */
            REQUIRE(t.lexeme.data() >= src.data());
            REQUIRE(t.lexeme.data() + t.lexeme.size() <= src.data() + src.size());

            /* At least one byte is consumed per token, so a stream can
             * never be longer than the input. */
            REQUIRE(produced <= src.size() + 1U);
        }

        /* EOF is stable: asking again still yields EOF. */
        const token eof = lx.next();
        CHECK(eof.type == token_type::eof);
    }
}
