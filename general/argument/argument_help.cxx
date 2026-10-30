#include <tracey/argument.hxx>
#include <tracey/config.hxx>

#include <print>

namespace tracey {

void print_help(const args_config& config, std::FILE* out)
{
    if (out == nullptr) out = stdout;

    std::print(out, "Usage: {} [options] [input_files...]\n", config.program_name);
    if (!config.description.empty()) std::print(out, "{}\n\n", config.description);
    std::print(out, "Options:\n");
    std::print(out, "  -h, --help                 Show this help message\n");
    std::print(out, "  -V, --verbose              Enable verbose stage logging\n");
    std::print(out, "  -v, --version              Show detailed version information\n");
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
    std::print(out, "Target: {}\n", tracey::config::default_target);
    std::print(out, "ABI: {}-bit\n", tracey::config::abi_bits);
    std::print(out, "Toolchain: {}\n", __VERSION__);
    std::print(out, "Built: {} {}\n", __DATE__, __TIME__);
    std::print(out, "Build configuration:\n");
    std::print(out, "  Debug trace: {}\n", tracey::config::debug_trace ? "enabled" : "disabled");
    std::print(out, "  zlib: {}\n", tracey::config::use_zlib ? "enabled" : "disabled");
    std::print(out, "  threads: {}\n", tracey::config::use_threads ? "enabled" : "disabled");
    std::print(out, "  max source size: {}\n", tracey::config::max_source_size);
    std::print(out, "  stack size: {}\n", tracey::config::stack_size);
    std::print(out, "Modules:\n");
    std::print(out, "  parser: {}\n", tracey::config::module_parser ? "enabled" : "disabled");
    std::print(out, "  semantic: {}\n", tracey::config::module_semantic ? "enabled" : "disabled");
    std::print(out, "  codegen: {}\n", tracey::config::module_codegen ? "enabled" : "disabled");
    std::print(out, "  assembler: {}\n", tracey::config::module_assembler ? "enabled" : "disabled");
    std::print(out, "  linker: {}\n", tracey::config::module_linker ? "enabled" : "disabled");
}

} /** namespace tracey */
