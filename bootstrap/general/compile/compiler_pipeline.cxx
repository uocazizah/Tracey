#include <cstdio>
#include <tracey/compile/compile.hxx>
#include <tracey/file/file.hxx>

int compile(TraceyOptions *opts)
{
    TraceyFile file = {};

    if (tracey_file_open(&file, opts->input_file) != 0)
    {
        fprintf(stderr,
                "failed to open '%s'\n",
                opts->input_file);

        return 1;
    }

   tracey_debug_file_print(&file);

   tracey_file_close(&file);

    return 0;
}
