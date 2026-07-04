#include <tracey/argument/argument.hxx>
#include <tracey/compile/compile.hxx>

int main(int argc, char **argv)
{
    TraceyOptions opts = {0};

   if(!parse_args(argc, argv, &opts))
   {
      return 1;
   }

    debug_print_args(&opts);

    compile(&opts);

    return 0;
}
