#ifndef ARGUMENT_H
#define ARGUMENT_H

#include <tracey/struct/args_struct.h>

int parse_args(int argc, char **argv, TraceyOptions *opts);
void debug_print_args(const TraceyOptions *opts);

#endif /* ARGUMENT_H */
