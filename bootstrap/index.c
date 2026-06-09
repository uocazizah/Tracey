#include <stdio.h>
#include <string.h>

typedef struct TraceyOptions {
    const char *input_file;

    int warning_all;
    int warning_extra;

    const char *output_file;
} TraceyOptions;

int parse_args(int argc, char **argv, TraceyOptions *opts)
{
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-Wall") == 0) {
            opts->warning_all = 1;
        }
        else if (strcmp(argv[i], "-Wextra") == 0) {
            opts->warning_extra = 1;
        }
        else if (strcmp(argv[i], "-o") == 0) {
            if (++i >= argc)
                return 0;

            opts->output_file = argv[i];
        }
        else {
            opts->input_file = argv[i];
        }
    }

    return 1;
}

void compile(const TraceyOptions *opts)
{
    printf("Input  : %s\n", opts->input_file);
    printf("Output : %s\n", opts->output_file);
    printf("Wall   : %d\n", opts->warning_all);
    printf("Wextra : %d\n", opts->warning_extra);
}

int main(int argc, char **argv)
{
    TraceyOptions opts = {0};

    if (!parse_args(argc, argv, &opts))
    {
        fprintf(stderr, "Invalid arguments\n");
        return 1;
    }

    compile(&opts);

    return 0;
}
