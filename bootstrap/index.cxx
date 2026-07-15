#include <tracey/argument/argument.hxx>
#include <tracey/compile/compile.hxx>

int main(int argc, char **argv)
{
    tracey::argument::argument_option option;

   if(option.parse_args(argc, argv))
   {
      return 1;
   }

    option.debug_print_args();

    compile(&option);

    return 0;
}
