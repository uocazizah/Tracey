#include <stdio.h>
#include <tracey/argument/args.h>

int main(int argc, char **argv)
{
    TraceyOptions opts = {0};

    parse_args(argc, argv, &opts);

    debug_print_args(&opts);

  /* compile(&opts); */

    return 0;
}
