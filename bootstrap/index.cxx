#include <tracey/argument.hxx>

#include <span>

int main(int argc, char **argv)
{

   tracey::argument::argument_option options;

    std::span<const char* const> args(argv + 1, static_cast<std::size_t>(argc - 1));

    auto result = options.parse_args(args);

    options.debug_print_args();

    return 0;
}
