#include <cstdio>
#include <tracey/compile/compile.hxx>
#include <tracey/file/file.hxx>

int compile(tracey::argument::argument_option *option)
{
    tracey::file::TraceyFile file;

    if (!file.open(option->get_input_file()))
    {
        std::fprintf(stderr,
                     "failed to open '%s'\n",
                     option->get_input_file());
        return 1;
    }

    file.debug_print();

    file.close();

    return 0;
}
