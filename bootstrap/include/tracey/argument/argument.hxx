#pragma once

#include <tracey/struct/args_struct.hxx>

int parse_args(int argc, char **argv, TraceyOptions *opts);
void debug_print_args(const TraceyOptions *opts);
