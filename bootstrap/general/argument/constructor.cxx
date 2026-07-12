#include <tracey/argument/argument.hxx>

tracey::argument::argument_option::argument_option() noexcept
    : p_input_file(nullptr),
      p_output_file(nullptr),
      warning_all(false),
      warning_extra(false)
{
}
