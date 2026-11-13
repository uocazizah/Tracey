#ifndef TRACEY_TEST_HARNESS_H
#define TRACEY_TEST_HARNESS_H

/*
 * Minimal C17 test harness for the Tracey test executables.
 *
 * This header replaces the doctest framework used by the former C++ test
 * suite. It intentionally provides only the subset of functionality the
 * compiler tests need: registration-free test cases, non-fatal CHECK
 * assertions, fatal REQUIRE assertions, equality helpers and a main()
 * driver built from three macros.
 *
 * Each test executable is a single translation unit that includes this
 * header exactly once. The counters therefore live in static storage and
 * no runtime library is required. A failed CHECK records a failure and
 * lets the remaining assertions and test cases run; a failed REQUIRE
 * returns from the current test case, mirroring doctest's REQUIRE.
 *
 * Dependencies are limited to <stdbool.h>, <stddef.h>, <stdio.h> and
 * <string.h>; nothing is allocated by the harness itself.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

/*
 * Assertion counters shared by all test cases in the translation unit.
 *
 * checks counts every evaluated assertion, failures counts the ones that
 * did not hold. Both are reset by program start; TEST_MAIN_END reads them
 * after all cases have run.
 */
static size_t tracey_test_checks = 0;
static size_t tracey_test_failures = 0;

/*
 * tracey_test_report - print one assertion failure diagnostic.
 *
 * what is the textual form of the failed assertion and must be a
 * NUL-terminated string with static lifetime, normally produced by the
 * preprocessor stringification operator. The diagnostic is written to
 * stderr in "file:line: FAILED: expression" form.
 */
static inline void tracey_test_report(const char *file, int line, const char *what)
{
    fprintf(stderr, "%s:%d: FAILED: %s\n", file, line, what);
}

/*
 * tracey_test_streq - NULL-safe string comparison for CHECK_STR_EQ.
 *
 * Returns true when both arguments are NULL, or when both are non-NULL
 * and compare equal with strcmp. A single NULL argument compares unequal
 * to any non-NULL string. Neither argument is modified.
 */
static inline bool tracey_test_streq(const char *lhs, const char *rhs)
{
    if (lhs == NULL || rhs == NULL)
        return lhs == rhs;
    return strcmp(lhs, rhs) == 0;
}

/*
 * TEST_CASE - declare a test case function.
 *
 * The caller supplies a valid C identifier and an immediately following
 * function body, for example:
 *
 *     TEST_CASE(lexer_keywords)
 *     {
 *         CHECK(...);
 *     }
 */
#define TEST_CASE(name) static void name(void)

/*
 * CHECK - evaluate a condition and record a failure without aborting.
 *
 * The expression is evaluated exactly once. On failure a diagnostic is
 * printed to stderr and the failure counter is incremented; execution
 * continues with the next assertion so that one case reports every
 * problem it can observe.
 */
#define CHECK(expr)                                                              \
    do                                                                           \
    {                                                                            \
        tracey_test_checks++;                                                    \
        if (!(expr))                                                             \
        {                                                                        \
            tracey_test_report(__FILE__, __LINE__, #expr);                       \
            tracey_test_failures++;                                              \
        }                                                                        \
    } while (0)

/*
 * CHECK_FALSE - CHECK for a condition that must not hold.
 */
#define CHECK_FALSE(expr)                                                        \
    do                                                                           \
    {                                                                            \
        tracey_test_checks++;                                                    \
        if ((expr))                                                              \
        {                                                                        \
            tracey_test_report(__FILE__, __LINE__, "!(" #expr ")");              \
            tracey_test_failures++;                                              \
        }                                                                        \
    } while (0)

/*
 * CHECK_EQ - compare two scalar values with the equality operator.
 *
 * Both values are evaluated exactly once. The diagnostic prints the
 * textual operands rather than their values so that the macro works for
 * enums, integers and pointers without format specifier assumptions.
 */
#define CHECK_EQ(lhs, rhs)                                                       \
    do                                                                           \
    {                                                                            \
        tracey_test_checks++;                                                    \
        if (!((lhs) == (rhs)))                                                   \
        {                                                                        \
            tracey_test_report(__FILE__, __LINE__, #lhs " == " #rhs);            \
            tracey_test_failures++;                                              \
        }                                                                        \
    } while (0)

/*
 * CHECK_STR_EQ - compare two NUL-terminated strings with tracey_test_streq.
 *
 * NULL compares equal only to NULL.
 */
#define CHECK_STR_EQ(lhs, rhs)                                                   \
    do                                                                           \
    {                                                                            \
        tracey_test_checks++;                                                    \
        if (!tracey_test_streq((lhs), (rhs)))                                    \
        {                                                                        \
            tracey_test_report(__FILE__, __LINE__,                               \
                               "strcmp(" #lhs ", " #rhs ") == 0");               \
            tracey_test_failures++;                                              \
        }                                                                        \
    } while (0)

/*
 * REQUIRE - fatal variant of CHECK.
 *
 * A failed REQUIRE records the failure and returns from the enclosing
 * test case, so REQUIRE may only appear inside TEST_CASE functions. It is
 * intended for preconditions that would make the rest of the case
 * meaningless or unsafe, such as a successful token-count check before
 * indexing the token array.
 */
#define REQUIRE(expr)                                                            \
    do                                                                           \
    {                                                                            \
        tracey_test_checks++;                                                    \
        if (!(expr))                                                             \
        {                                                                        \
            tracey_test_report(__FILE__, __LINE__, #expr);                       \
            tracey_test_failures++;                                              \
            return;                                                              \
        }                                                                        \
    } while (0)

/*
 * TEST_MAIN_BEGIN / TEST_RUN / TEST_MAIN_END - test executable driver.
 *
 * TEST_MAIN_BEGIN opens main() and stores the suite name used in the
 * summary line. TEST_RUN increments the case counter and invokes one test
 * function. TEST_MAIN_END prints the summary and returns non-zero when any
 * assertion failed, which is the convention the build system expects.
 */
#define TEST_MAIN_BEGIN(suite)                                                   \
    int main(void)                                                               \
    {                                                                            \
        const char *tracey_test_suite = (suite);                                 \
        int tracey_test_cases = 0;

#define TEST_RUN(fn)                                                             \
    do                                                                           \
    {                                                                            \
        tracey_test_cases++;                                                     \
        fn();                                                                    \
    } while (0)

#define TEST_MAIN_END                                                            \
    fprintf(stdout, "[%s] %d cases, %zu checks, %zu failures\n",                 \
            tracey_test_suite, tracey_test_cases,                                \
            tracey_test_checks, tracey_test_failures);                           \
    return tracey_test_failures != 0 ? 1 : 0;                                    \
    }

#endif /* TRACEY_TEST_HARNESS_H */
