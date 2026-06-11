#ifndef ARGS_STRUCT_H
#define ARGS_STRUCT_H

typedef struct TraceyOptions
{
    const char *input_file;
    int warning_all;
    int warning_extra;
    const char *output_file;
} TraceyOptions;

#endif /* ARGS_STRUCT_H */
