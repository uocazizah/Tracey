#include <cstdio>
#include <cstring>
#include <tracey/error/error.hxx>
#include <tracey/argument/argument.hxx>

/* Returns 1 if filename ends with ".trc", 0 otherwise. */
static int has_trc_extension(const char *filename)
{
    const char *p_ext = strrchr(filename, '.');

    if (!p_ext) {
        return 0;
    }

    return strcmp(p_ext, ".trc") == 0;
}

int parse_args(int argc, char **argv, TraceyOptions *p_opts)
{
    for (int i = 1; i < argc; i++) {
        const char *arg = argv[i];

        if (strcmp(arg, "-Wall") == 0) {
            p_opts->warning_all = 1;
        }
        else if (strcmp(arg, "-Wextra") == 0) {
            p_opts->warning_extra = 1;
        }
        else if (strcmp(arg, "-o") == 0) {
            if (++i >= argc || argv[i][0] == '-') {
                fatal("option '-o' requires an output filename");
                return 0;
            }

            p_opts->output_file = argv[i];
        }
        else if (arg[0] == '-') {
            /* Unknown flag: don't silently swallow it as input_file. */
            fatal("unrecognized option '%s'", arg);
            return 0;
        }
        else {
            if (p_opts->input_file) {
                fatal("multiple input files specified ('%s' and '%s')",
                      p_opts->input_file, arg);
                return 0;
            }

            p_opts->input_file = arg;
        }
    }

    if (!p_opts->input_file) {
        warning("no input file specified");
        fatal("compilation terminated");
        return 0;
    }

    if (!has_trc_extension(p_opts->input_file)) {
        warning("input file '%s' is not valid", p_opts->input_file);
        fatal("compilation terminated");
        return 0;
    }

    return 1;
}
