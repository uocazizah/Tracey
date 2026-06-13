#include <stdio.h>
#include <string.h>
#include <tracey/argument/argument.h>

int parse_args(int argc, char **argv, TraceyOptions *opts) {
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-Wall") == 0) {
            opts->warning_all = 1;
        }
        else if (strcmp(argv[i], "-Wextra") == 0) {
            opts->warning_extra = 1;
        }
        else if (strcmp(argv[i], "-o") == 0) {
            if (++i >= argc) return 0;
            opts->output_file = argv[i];
        }
        else {
            opts->input_file = argv[i];
        }
    }
    return 1;
}
