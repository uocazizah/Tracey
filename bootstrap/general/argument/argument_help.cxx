#include <tracey/argument.hxx>

#include <print>

namespace tracey {

void print_help(const args_config& config, std::FILE* out)
{
    if (out == nullptr) out = stdout;

    std::print(out, "Usage: {} [options] [input_files...]\n", config.program_name);
    if (!config.description.empty()) std::print(out, "{}\n\n", config.description);
    std::print(out, "Options:\n");
    std::print(out, "  -h, --help                 Show this help message\n");
    std::print(out, "  --version                  Show version information\n");
    std::print(out, "  -v, --verbose              Enable verbose output\n");
    std::print(out, "  -o, --output <file>        Specify output file\n");
    std::print(out, "  -I, --include <path>       Add include path (can be repeated)\n");
    std::print(out, "  -D, --define <macro>       Define a macro (can be repeated)\n");
    std::print(out, "  -O<level>                  Set optimization level (0-3)\n");
    std::print(out, "  --optimize <level>         Set optimization level (0-3)\n");
    std::print(out, "  -Werror                    Treat warnings as errors\n");
    std::print(out, "  -g                         Generate debug information\n");
}

void print_version(const args_config& config, std::FILE* out)
{
    if (out == nullptr) out = stdout;
    std::print(out, "{} version {}\n", config.program_name, config.version_string);
}

} /* namespace tracey */
