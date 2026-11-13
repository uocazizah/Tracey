/*
 * Command-line argument parsing unit tests.
 *
 * Each case builds an argv vector in memory, calls tracey_args_parse and
 * inspects the resulting structure. Successful parses own heap copies of
 * every stored string and are always released with tracey_args_free.
 * Failed parses leave the structure in a safe empty state; calling
 * tracey_args_free after a failure is still valid and is done where the
 * test inspects that state.
 *
 * argv arrays contain string literals, which have static lifetime, so the
 * parser never receives dangling input.
 */

#include "test_harness.h"

#include <tracey/argument.h>

#include <string.h>

/* Retained from the original placeholder test to keep its coverage. */
TEST_CASE(argument_stub)
{
    CHECK(true);
}

TEST_CASE(argument_parses_input_file)
{
    char *argv[] = {"tracey", "input.trc"};
    struct tracey_args args;

    CHECK_EQ(tracey_args_parse(2, argv, NULL, &args), TRACEY_ARGS_OK);
    CHECK_EQ(args.input_count, 1U);
    CHECK_STR_EQ(args.inputs[0], "input.trc");
    CHECK_EQ(args.optimization, TRACEY_OPT_NONE);
    CHECK_STR_EQ(args.program_name, "tracey");

    tracey_args_free(&args);
}

TEST_CASE(argument_parses_flags_and_values)
{
    char *argv[] = {"tracey", "-O2", "-I", "inc", "-D", "FOO=1", "-D", "BAR",
                    "-o",     "out.o", "-g",  "-Werror", "-V",
                    "-S",     "src.trc"};
    int argc = (int)(sizeof(argv) / sizeof(argv[0]));
    struct tracey_args args;

    CHECK_EQ(tracey_args_parse(argc, argv, NULL, &args), TRACEY_ARGS_OK);
    CHECK_EQ(args.optimization, TRACEY_OPT_LEVEL2);
    CHECK(tracey_args_has_include_path(&args, "inc"));
    CHECK_FALSE(tracey_args_has_include_path(&args, "missing"));
    CHECK_STR_EQ(tracey_args_find_define(&args, "FOO"), "FOO=1");
    CHECK_STR_EQ(tracey_args_find_define(&args, "BAR"), "BAR");
    CHECK(tracey_args_find_define(&args, "MISSING") == NULL);
    CHECK_STR_EQ(args.output, "out.o");
    CHECK(args.debug_info);
    CHECK(args.werror);
    CHECK(args.verbose);
    CHECK(args.assembly);
    CHECK_EQ(args.input_count, 1U);
    CHECK_STR_EQ(args.inputs[0], "src.trc");

    tracey_args_free(&args);
}

TEST_CASE(argument_parses_help)
{
    char *argv[] = {"tracey", "--help"};
    struct tracey_args args;

    CHECK_EQ(tracey_args_parse(2, argv, NULL, &args), TRACEY_ARGS_OK);
    CHECK(args.help);
    CHECK_FALSE(args.version);

    tracey_args_free(&args);
}

TEST_CASE(argument_parses_version)
{
    char *argv[] = {"tracey", "-v"};
    struct tracey_args args;

    CHECK_EQ(tracey_args_parse(2, argv, NULL, &args), TRACEY_ARGS_OK);
    CHECK(args.version);
    CHECK_FALSE(args.help);

    tracey_args_free(&args);
}

TEST_CASE(argument_reports_missing_value)
{
    char *argv[] = {"tracey", "-o"};
    struct tracey_args args;

    CHECK_EQ(tracey_args_parse(2, argv, NULL, &args), TRACEY_ARGS_MISSING_ARG);
    CHECK_EQ(args.input_count, 0U);
    CHECK_STR_EQ(args.output, NULL);

    tracey_args_free(&args);
}

TEST_CASE(argument_reports_empty_value)
{
    char *argv[] = {"tracey", ""};
    struct tracey_args args;

    CHECK_EQ(tracey_args_parse(2, argv, NULL, &args), TRACEY_ARGS_EMPTY_ARG);
    CHECK_EQ(args.input_count, 0U);

    tracey_args_free(&args);
}

TEST_CASE(argument_rejects_unknown_option)
{
    char *argv[] = {"tracey", "--bogus"};
    struct tracey_args args;

    CHECK_EQ(tracey_args_parse(2, argv, NULL, &args), TRACEY_ARGS_INVALID);
    CHECK_EQ(args.input_count, 0U);

    tracey_args_free(&args);
}

TEST_CASE(argument_accepts_unknown_option_when_configured)
{
    char *argv[] = {"tracey", "--bogus"};
    struct tracey_args_config config = {"tracey", "1.0-test", NULL, 10000U, true};
    struct tracey_args args;

    CHECK_EQ(tracey_args_parse(2, argv, &config, &args), TRACEY_ARGS_OK);
    CHECK_EQ(args.input_count, 0U);
    CHECK_STR_EQ(args.version_string, "1.0-test");

    tracey_args_free(&args);
}

TEST_CASE(argument_enforces_max_args)
{
    char *argv[] = {"tracey", "input.trc"};
    struct tracey_args_config config = {"tracey", "1.0-test", NULL, 1U, false};
    struct tracey_args args;

    CHECK_EQ(tracey_args_parse(2, argv, &config, &args), TRACEY_ARGS_TOO_MANY);
    CHECK_EQ(args.input_count, 0U);

    tracey_args_free(&args);
}

TEST_CASE(argument_initialized_state_is_empty)
{
    struct tracey_args args;

    tracey_args_init(&args);
    CHECK_EQ(args.input_count, 0U);
    CHECK_EQ(args.include_path_count, 0U);
    CHECK_EQ(args.define_count, 0U);
    CHECK(args.inputs == NULL);
    CHECK(args.include_paths == NULL);
    CHECK(args.defines == NULL);
    CHECK(args.output == NULL);
    CHECK_EQ(args.optimization, TRACEY_OPT_NONE);
    CHECK_FALSE(args.help);
    CHECK_FALSE(args.version);
    CHECK_FALSE(args.verbose);
    CHECK_FALSE(args.werror);
    CHECK_FALSE(args.debug_info);
    CHECK_FALSE(args.assembly);

    tracey_args_free(&args);
}

TEST_CASE(argument_error_strings_are_stable)
{
    CHECK_STR_EQ(tracey_args_error_string(TRACEY_ARGS_OK), "No error");
    CHECK(tracey_args_error_string(TRACEY_ARGS_INVALID) != NULL);
    CHECK(tracey_args_error_string((enum tracey_args_error)999) != NULL);
}

TEST_MAIN_BEGIN("argument")
    TEST_RUN(argument_stub);
    TEST_RUN(argument_parses_input_file);
    TEST_RUN(argument_parses_flags_and_values);
    TEST_RUN(argument_parses_help);
    TEST_RUN(argument_parses_version);
    TEST_RUN(argument_reports_missing_value);
    TEST_RUN(argument_reports_empty_value);
    TEST_RUN(argument_rejects_unknown_option);
    TEST_RUN(argument_accepts_unknown_option_when_configured);
    TEST_RUN(argument_enforces_max_args);
    TEST_RUN(argument_initialized_state_is_empty);
    TEST_RUN(argument_error_strings_are_stable);
TEST_MAIN_END
