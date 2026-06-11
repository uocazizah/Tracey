#ifndef TRACEY_H
#define TRACEY_H

#include <tracey/struct/args_struct.h>

int parse_args(int argc, char **argv, TraceyOptions *opts);
void compile(const TraceyOptions *opts);

#endif
