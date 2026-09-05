#include <tracey/argument.h>
#include <stdio.h>

int main(int argc, char** argv)
{
    Arguments* args = arguments_parse(argc, argv);
    if (!args) {
        fprintf(stderr, "Failed to parse arguments\n");
        return 1;
    }

    if (arguments_get_help(args)) {
        arguments_print_help(argv[0]);
        arguments_free(args);
        return 0;
    }

    if (arguments_get_version(args)) {
        arguments_print_version(argv[0], "1.0.0");
        arguments_free(args);
        return 0;
    }

    // Untuk debugging, cukup panggil satu fungsi ini:
    arguments_print_debug(args);

    arguments_free(args);
    return 0;
}
