#include <tracey/argument.hxx>
#include <tracey/config.hxx>

#include <print>

namespace tracey
{

void print_help(const args_config &config, std::FILE *p_out)
{
    if (p_out == nullptr)
        p_out = stdout;

    std::print(p_out, "Usage: {} [options] [input_files...]\n", config.program_name);
    if (!config.description.empty())
        std::print(p_out, "{}\n\n", config.description);
    std::print(p_out, "Options:\n");
    std::print(p_out, "  -h, --help                 Show this help message\n");
    std::print(p_out, "  -V, --verbose              Enable verbose stage logging\n");
    std::print(p_out, "  -v, --version              Show detailed version information\n");
    std::print(p_out, "  -o, --output <file>        Specify output file\n");
    std::print(p_out, "  -I, --include <path>       Add include path (can be repeated)\n");
    std::print(p_out, "  -D, --define <macro>       Define a macro (can be repeated)\n");
    std::print(p_out, "  -O<level>                  Set optimization level (0-3)\n");
    std::print(p_out, "  --optimize <level>         Set optimization level (0-3)\n");
    std::print(p_out, "  -Werror                    Treat warnings as errors\n");
    std::print(p_out, "  -g                         Generate debug information\n");
}

void print_version(const args_config &config, std::FILE *p_out)
{
    if (p_out == nullptr)
        p_out = stdout;
    std::print(p_out, "{} version {}\n", config.program_name, config.version_string);
    std::print(p_out, "Target: {}\n", tracey::config::default_target);
    std::print(p_out, "ABI: {}-bit\n", tracey::config::abi_bits);
    std::print(p_out, "Toolchain: {}\n", __VERSION__);
    std::print(p_out, "Built: {} {}\n", __DATE__, __TIME__);
    std::print(p_out, "Build configuration:\n");
    std::print(p_out, "  Debug trace: {}\n", tracey::config::debug_trace ? "enabled" : "disabled");
    std::print(p_out, "  zlib: {}\n", tracey::config::use_zlib ? "enabled" : "disabled");
    std::print(p_out, "  threads: {}\n", tracey::config::use_threads ? "enabled" : "disabled");
    std::print(p_out, "  max source size: {}\n", tracey::config::max_source_size);
    std::print(p_out, "  stack size: {}\n", tracey::config::stack_size);
    std::print(p_out, "Modules:\n");
    std::print(p_out, "  parser: {}\n", tracey::config::module_parser ? "enabled" : "disabled");
    std::print(p_out, "  semantic: {}\n", tracey::config::module_semantic ? "enabled" : "disabled");
    std::print(p_out, "  codegen: {}\n", tracey::config::module_codegen ? "enabled" : "disabled");
    std::print(p_out, "  assembler: {}\n",
               tracey::config::module_assembler ? "enabled" : "disabled");
    std::print(p_out, "  linker: {}\n", tracey::config::module_linker ? "enabled" : "disabled");
}

} // namespace tracey
