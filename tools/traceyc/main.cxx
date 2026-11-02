#include <tracey/argument.hxx>
#include <tracey/assembler.hxx>
#include <tracey/config.hxx>
#include <tracey/elf.hxx>
#include <tracey/linker.hxx>
#include <tracey/pipeline.hxx>
#include <tracey/source.hxx>

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <print>
#include <set>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

int main(int argc, char **argv)
{
    tracey::args_config config;
    config.program_name = "traceyc";
    config.version_string = tracey::config::version;
    config.description = "Tracey compiler - compile .trc files";
    config.allow_unknown_options = false;
    config.max_args = 10000;

    auto parsed = tracey::args::parse(argc, argv, config);
    if (!parsed)
    {
        const tracey::args_error err = parsed.error();
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wcovered-switch-default"
#endif
        switch (err)
        {
            case tracey::args_error::nomem:
                std::print(stderr, "Error: out of memory while parsing command-line arguments\n");
                std::print(stderr, "\033[1;34mnote:\033[0m reduce the number of arguments or free memory\n");
                return 1;
            case tracey::args_error::too_many:
                std::print(stderr, "Error: too many command-line arguments (max {})\n", config.max_args);
                std::print(stderr, "\033[1;34mnote:\033[0m split the invocation or reduce input files\n");
                return 1;
            case tracey::args_error::missing_arg:
                std::print(stderr, "Error: missing required command-line argument\n");
                std::print(stderr, "\033[1;34mnote:\033[0m provide the missing option value\n");
                return 1;
            case tracey::args_error::empty_arg:
                std::print(stderr, "Error: empty command-line argument is not allowed\n");
                std::print(stderr, "\033[1;34mnote:\033[0m provide a non-empty value\n");
                return 1;
            case tracey::args_error::invalid:
            default:
                std::print(stderr, "Error: {}\n", tracey::to_string(err));
                std::print(stderr, "\033[1;34mnote:\033[0m review the command-line syntax\n");
                tracey::print_help(config, stderr);
                return 1;
        }
#if defined(__clang__)
#pragma clang diagnostic pop
#endif
    }

    const tracey::args &args = *parsed;
    const bool verbose = args.has_verbose();

    if (args.has_help())
    {
        tracey::print_help(config, stdout);
        return 0;
    }
    if (args.has_version())
    {
        tracey::print_version(config, stdout);
        return 0;
    }

    const auto inputs = args.inputs();
    if (inputs.empty())
    {
        std::print(stderr, "traceyc: \033[1;31mfatal error:\033[0m no input files provided\n");
        std::print(stderr, "traceyc: \033[1;34mnote:\033[0m pass at least one .trc source file\n");
        return 2;
    }
    std::vector<tracey::source> sources;
    sources.reserve(inputs.size());
    for (const auto &path : inputs)
    {
        auto src = tracey::source::read(path); /* flawfinder: ignore */
        if (!src)
        {
            std::print(stderr, "Error: failed to read input file '{}': {}\n", path,
                       tracey::to_string(src.error()));
            std::print(stderr, "\033[1;34mnote:\033[0m check file existence, permissions, and that it is a regular file\n");
            return 1;
        }
        sources.push_back(std::move(*src));
    }

    if (verbose)
    {
        args.print_debug(stderr);
        for (std::size_t i = 0; i < sources.size(); i++)
        {
            if (i > 0)
                std::print(stderr, "\n");
            sources[i].print_debug(stderr);
        }
    }

    // Build include paths
    std::vector<std::string> incs(args.include_paths().begin(), args.include_paths().end());
    incs.push_back("lib");
    incs.push_back("include");
#ifdef TRACEY_LIBDIR
    incs.push_back(TRACEY_LIBDIR);
#endif
#ifdef TRACEY_INCLUDEDIR
    incs.push_back(TRACEY_INCLUDEDIR);
#endif
    // Allow environment override for library/include locations. Environment
    // variables are untrusted input (CWE-807, CWE-20), so validate that
    // each value names an existing directory and canonicalize it before
    // use. Invalid or non-directory values are ignored with a warning.
    auto add_env_include_path = [&incs](const char *p_var_name)
    {
        const char *p_env = std::getenv(p_var_name); /* flawfinder: ignore */
        if (p_env == nullptr || p_env[0] == '\0')
            return;

        std::error_code ec;
        const std::filesystem::path candidate(p_env);
        if (!std::filesystem::is_directory(candidate, ec))
        {
            std::print(stderr, "Warning: ignoring environment variable {}='{}' because it is not a directory\n",
                       p_var_name, p_env);
            std::print(stderr, "\033[1;34mnote:\033[0m set it to an existing directory or unset it\n");
            return;
        }

        const std::filesystem::path canonical =
            std::filesystem::canonical(candidate, ec);
        if (ec)
        {
            std::print(stderr, "Warning: ignoring environment variable {}='{}': {}\n", p_var_name, p_env,
                       ec.message());
            std::print(stderr, "\033[1;34mnote:\033[0m check directory permissions and canonical path\n");
            return;
        }

        incs.push_back(canonical.string());
    };

    add_env_include_path("TRACEY_LIB_PATH");
    add_env_include_path("TRACEY_INCLUDE_PATH");

    // Set up pipeline
    tracey::compile_options opts;
    opts.arch = tracey::target_arch::aarch64;
    opts.verbose = verbose;
    opts.include_paths = incs;

    tracey::compile_pipeline pipeline(opts);

    bool had_error = false;
    std::string generated;

    for (std::size_t i = 0; i < sources.size(); i++)
    {
        const std::string &path = inputs[i];
        const tracey::source &src = sources[i];

        bool emit_rt = (i + 1 == sources.size());
        auto result = pipeline.process_file(src, path, i, emit_rt);
        if (result.had_error)
        {
            had_error = true;
            continue;
        }
        generated += result.assembly;
    }

    if (had_error)
        return 1;

    if (args.has_assembly())
    {
        if (args.output())
        {
            std::ofstream f(std::string(*args.output()));
            if (!f)
            {
                std::print(stderr, "Error: cannot write output file\n");
                std::print(stderr, "\033[1;34mnote:\033[0m check output path and permissions\n");
                return 1;
            }
            f << generated;
        }
        else
        {
            std::print(stdout, "{}", generated);
        }
        return 0;
    }

    std::string exe_path = args.output() ? std::string(*args.output()) : std::string("a.out");

    // Write assembly to a temporary file.
    namespace fs = std::filesystem;
    fs::path tmp_dir = fs::temp_directory_path();
    fs::path asm_path = tmp_dir / "traceyc.s";
    fs::path obj_path = tmp_dir / "traceyc.o";

    {
        std::ofstream f(asm_path.c_str());
        if (!f)
        {
            std::print(stderr, "Error: cannot write temporary assembly file\n");
            std::print(stderr, "\033[1;34mnote:\033[0m check temporary directory permissions\n");
            return 1;
        }
        f << generated;
    }

    if (verbose)
        std::print(stderr, "[assemble] start\n");
    auto t_asm = std::chrono::steady_clock::now();
    // Assemble
    auto asm_result = tracey::assemble_aarch64(generated);
    if (verbose)
    {
        auto ms =
            std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t_asm)
                .count();
        std::print(stderr, "[assemble] done ({:.3f} ms)\n", ms);
    }
    if (!asm_result.ok)
    {
        std::print(stderr, "Error: assembler failed: {}\n", asm_result.error);
        std::print(stderr, "\033[1;34mnote:\033[0m inspect generated assembly around the reported instruction\n");
        return 1;
    }
    if (!tracey::write_elf_object(obj_path.string(), asm_result))
    {
        std::print(stderr, "Error: cannot write object file\n");
        std::print(stderr, "\033[1;34mnote:\033[0m check temporary directory permissions\n");
        return 1;
    }

    if (verbose)
        std::print(stderr, "[link] start\n");
    auto t_link = std::chrono::steady_clock::now();
    // Link
    std::vector<std::string> objs = {obj_path.string()};
    if (!tracey::link_elf_aarch64(objs, exe_path))
    {
        std::print(stderr, "Error: linking failed\n");
        std::print(stderr, "\033[1;34mnote:\033[0m verify object files, entry point, and unresolved symbols\n");
        return 1;
    }

    // Make executable
    std::error_code ec;
    fs::permissions(exe_path,
                    fs::perms::owner_exec | fs::perms::group_exec | fs::perms::others_exec,
                    fs::perm_options::add, ec);

    if (verbose)
    {
        auto ms =
            std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t_link)
                .count();
        std::print(stderr, "[link] done ({:.3f} ms)\n", ms);
    }
    std::print(stdout, "Output: {}\n", exe_path);

    // Cleanup
    fs::remove(asm_path, ec);
    fs::remove(obj_path, ec);

    return 0;
}
