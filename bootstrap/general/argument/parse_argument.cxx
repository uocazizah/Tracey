#include <tracey/argument/argument.hxx>
#include <cstdio>
#include <cstring>

int tracey::argument::argument_option::parse_args(int argc, char **argv)
{
    p_input_file = nullptr;
    p_output_file = nullptr;
    warning_all = false;
    warning_extra = false;

    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "-Wall") == 0) {
            warning_all = true;
        } else if (std::strcmp(argv[i], "-Wextra") == 0) {
            warning_extra = true;
        } else if (std::strcmp(argv[i], "-o") == 0) {
            ++i;
            if (i < argc) {
                p_output_file = argv[i];
            } else {
                std::fprintf(stderr, "Error: -o requires an argument\n");
                return 1;
            }
        } else if (argv[i][0] != '-') {
            if (p_input_file == nullptr) {
                p_input_file = argv[i];
            } else {
                std::fprintf(stderr, "Error: multiple input files specified\n");
                return 1;
            }
        } else {
            std::fprintf(stderr, "Error: unknown option %s\n", argv[i]);
            return 1;
        }
    }

    if (p_input_file == nullptr) {
        std::fprintf(stderr, "Error: no input file specified\n");
        return 1;
    }

    return 0;
}
