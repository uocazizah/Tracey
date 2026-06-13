#include <stdio.h>
#include <tracey/argument/argument.h>

void debug_print_args(const TraceyOptions *opts)
{
    printf("Input  : %s\n", opts->input_file);
    printf("Output : %s\n", opts->output_file);
    printf("Wall   : %d\n", opts->warning_all);
    printf("Wextra : %d\n", opts->warning_extra);

}
