#include <tracey/argument.hxx>
#include <tracey/assembler.hxx>
#include <tracey/ast.hxx>
#include <tracey/codegen.hxx>
#include <tracey/config.hxx>
#include <tracey/lexer.hxx>
#include <tracey/linker.hxx>
#include <tracey/parser.hxx>
#include <tracey/semantic.hxx>
#include <tracey/source.hxx>

#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <print>
#include <string>
#include <utility>
#include <vector>

int main(int argc, char** argv)
{
    tracey::args_config config;
    config.program_name = "traceyc";
    config.version_string = tracey::config::version;
    config.description = "Tracey compiler - compile .trc files";
    config.allow_unknown_options = false;
    config.max_args = 10000;

    auto parsed = tracey::args::parse(argc, argv, config);
    if (!parsed) {
        const tracey::args_error err = parsed.error();
#if defined(__clang__)
        #pragma clang diagnostic push
        #pragma clang diagnostic ignored "-Wcovered-switch-default"
#endif
        switch (err) {
            case tracey::args_error::nomem:
                std::print(stderr, "Error: Out of memory\n");
                return 1;
            case tracey::args_error::too_many:
                std::print(stderr, "Error: Too many arguments (max {})\n", config.max_args);
                return 1;
            case tracey::args_error::missing_arg:
                std::print(stderr, "Error: Missing required argument\n");
                return 1;
            case tracey::args_error::empty_arg:
                std::print(stderr, "Error: Empty argument not allowed\n");
                return 1;
            case tracey::args_error::invalid:
            default:
                std::print(stderr, "Error: {}\n", tracey::to_string(err));
                tracey::print_help(config, stderr);
                return 1;
        }
#if defined(__clang__)
        #pragma clang diagnostic pop
#endif
    }

    const tracey::args& args = *parsed;

    if (args.has_help()) {
        tracey::print_help(config, stdout);
        return 0;
    }
    if (args.has_version()) {
        tracey::print_version(config, stdout);
        return 0;
    }

    const auto inputs = args.inputs();
    std::vector<tracey::source> sources;
    sources.reserve(inputs.size());
    for (const auto& path : inputs) {
        auto src = tracey::source::read(path);
        if (!src) {
            std::print(stderr, "Error: Failed to read input file '{}': {}\n",
                       path, tracey::to_string(src.error()));
            return 1;
        }
        sources.push_back(std::move(*src));
    }

    if (args.has_verbose()) {
        args.print_debug(stdout);
        for (std::size_t i = 0; i < sources.size(); i++) {
            if (i > 0) std::print(stdout, "\n");
            sources[i].print_debug(stdout);
        }
    }

    bool had_error = false;
    tracey::target_arch arch = tracey::target_arch::aarch64;
    // Kconfig default target could be checked, but keep native aarch64 for now.
    tracey::code_generator codegen(arch);

    std::ostringstream output;
    // We'll generate per input; for multiple inputs, concatenate.
    std::string generated;

    for (std::size_t i = 0; i < sources.size(); i++) {
        const std::string& path = inputs[i];
        const tracey::source& src = sources[i];

        // Lex
        tracey::lexer lx{src.content()};
        std::size_t lex_errors = 0;
        constexpr std::size_t max_lexer_errors = 10;
        // We need tokens for parser; but lexer is consumed by parser directly.
        // First check lexer errors by scanning? Parser will encounter error tokens.
        // We'll let parser handle; but driver previously reported lexer errors.
        // To preserve behavior, scan and store? Simpler: parser stops on error token.
        // We'll pre-scan to report lexer errors.
        {
            tracey::lexer lx2{src.content()};
            for (;;) {
                tracey::token t = lx2.next();
                if (t.is_error()) {
                    tracey::print_error(t, path, stderr);
                    had_error = true;
                    if (++lex_errors >= max_lexer_errors) {
                        std::print(stderr, "Error: {}: too many lexer errors, stopping\n", path);
                        break;
                    }
                    continue;
                }
                if (t.type == tracey::token_type::eof) break;
            }
            if (lex_errors > 0) continue; // skip parse if lexer errors
        }

        // Parse
        tracey::lexer lx3{src.content()};
        tracey::parser p(lx3);
        tracey::program prog;
        if (!p.parse(prog)) {
            for (auto& e : p.errors()) {
                std::print(stderr, "Error: {}:{}:{}: {}\n", path, e.line, e.column, e.message);
            }
            had_error = true;
            continue;
        }

        // Semantic
        tracey::semantic_analyzer sem;
        if (!sem.analyze(prog)) {
            for (auto& e : sem.errors()) {
                std::print(stderr, "Error: {}:{}:{}: {}\n", path, e.line, e.column, e.message);
            }
            had_error = true;
            continue;
        }

        // Codegen
        std::ostringstream ss;
        codegen.generate(prog, ss);
        generated += ss.str();
    }

    if (had_error) return 1;

    std::string exe_path = args.output() ? std::string(*args.output()) : std::string("a.out");

    // Write assembly to a temporary file.
    namespace fs = std::filesystem;
    fs::path tmp_dir = fs::temp_directory_path();
    fs::path asm_path = tmp_dir / "traceyc.s";
    fs::path obj_path = tmp_dir / "traceyc.o";

    {
        std::ofstream f(asm_path.c_str());
        if (!f) {
            std::print(stderr, "Error: cannot write temporary assembly file\n");
            return 1;
        }
        f << generated;
    }

    // Assemble
    auto asm_result = tracey::assemble_aarch64(generated);
    if (!asm_result.ok) {
        std::print(stderr, "Assembler error: {}\n", asm_result.error);
        return 1;
    }
    if (!tracey::write_elf_object(obj_path.string(), asm_result)) {
        std::print(stderr, "Error: cannot write object file\n");
        return 1;
    }

    // Link
    std::vector<std::string> objs = { obj_path.string() };
    if (!tracey::link_elf_aarch64(objs, exe_path)) {
        std::print(stderr, "Error: linking failed\n");
        return 1;
    }

    // Make executable
    std::error_code ec;
    fs::permissions(exe_path, fs::perms::owner_exec | fs::perms::group_exec | fs::perms::others_exec,
                    fs::perm_options::add, ec);

    std::print(stdout, "Output: {}\n", exe_path);

    // Cleanup
    fs::remove(asm_path, ec);
    fs::remove(obj_path, ec);

    return 0;
}
