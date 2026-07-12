#include <tracey/argument/argument.hxx>
#include <cstdio>

void tracey::argument::argument_option::debug_print_args() const
{
    std::fprintf(stderr, "Input file: %s\n",
                 p_input_file ? p_input_file : "(none)");
    std::fprintf(stderr, "Output file: %s\n",
                 p_output_file ? p_output_file : "(none)");
    std::fprintf(stderr, "Warning all: %s\n",
                 warning_all ? "true" : "false");
    std::fprintf(stderr, "Warning extra: %s\n",
                 warning_extra ? "true" : "false");
}
