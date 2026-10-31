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

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <functional>
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
        std::print(stderr, "traceyc: \033[1;31mfatal error:\033[0m no input file\n");
        return 2;
    }
    std::vector<tracey::source> sources;
    sources.reserve(inputs.size());
    for (const auto &path : inputs)
    {
        auto src = tracey::source::read(path);
        if (!src)
        {
            std::print(stderr, "Error: Failed to read input file '{}': {}\n", path,
                       tracey::to_string(src.error()));
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

    bool had_error = false;

    // Preprocessor: resolve #include
    std::function<std::string(const std::string &, const std::string &,
                              const std::vector<std::string> &, std::set<std::string> &)>
        preprocess = [&](const std::string &content, const std::string &base_dir,
                         const std::vector<std::string> &incs,
                         std::set<std::string> &visited) -> std::string
    {
        std::istringstream in(content);
        std::ostringstream out;
        std::string line;
        while (std::getline(in, line))
        {
            std::string t = line;
            // trim
            size_t a = t.find_first_not_of(" \\t");
            if (a != std::string::npos)
                t = t.substr(a);
            if (t.rfind("#include", 0) == 0)
            {
                size_t lt = t.find('<');
                size_t gt = t.find('>');
                size_t q1 = t.find('"');
                size_t q2 = (q1 != std::string::npos) ? t.find('"', q1 + 1) : std::string::npos;
                std::string incname;
                bool system = false;
                if (lt != std::string::npos && gt != std::string::npos)
                {
                    incname = t.substr(lt + 1, gt - lt - 1);
                    system = true;
                }
                else if (q1 != std::string::npos && q2 != std::string::npos)
                {
                    incname = t.substr(q1 + 1, q2 - q1 - 1);
                }
                if (!incname.empty())
                {
                    std::vector<std::string> search;
                    if (!system)
                        search.push_back(base_dir);
                    for (auto &p : incs)
                        search.push_back(p);
                    if (system)
                    {
                        search.push_back("lib");
                        search.push_back("include");
                    }
                    std::string found;
                    for (auto &d : search)
                    {
                        std::filesystem::path cand = std::filesystem::path(d) / incname;
                        if (std::filesystem::exists(cand))
                        {
                            found = cand.string();
                            break;
                        }
                    }
                    if (found.empty())
                    {
                        std::print(stderr, "Error: cannot find include '{}'\n", incname);
                        had_error = true;
                        continue;
                    }
                    if (visited.count(found))
                        continue; // include guard
                    visited.insert(found);
                    std::ifstream f(found);
                    std::ostringstream ss;
                    ss << f.rdbuf();
                    std::string incdir = std::filesystem::path(found).parent_path().string();
                    out << preprocess(ss.str(), incdir, incs, visited) << "\n";
                    continue;
                }
            }
            out << line << "\n";
        }
        return out.str();
    };

    tracey::target_arch arch = tracey::target_arch::aarch64;
    // Kconfig default target could be checked, but keep native aarch64 for now.
    tracey::code_generator codegen(arch);

    std::ostringstream output;
    // We'll generate per input; for multiple inputs, concatenate.
    std::string generated;

    for (std::size_t i = 0; i < sources.size(); i++)
    {
        const std::string &path = inputs[i];
        const tracey::source &src = sources[i];

        std::set<std::string> visited;
        std::vector<std::string> incs(args.include_paths().begin(), args.include_paths().end());
        incs.push_back("lib");
        incs.push_back("include");
#ifdef TRACEY_LIBDIR
        incs.push_back(TRACEY_LIBDIR);
#endif
#ifdef TRACEY_INCLUDEDIR
        incs.push_back(TRACEY_INCLUDEDIR);
#endif
        // Allow environment override for library/include locations.
        if (const char *p_env = std::getenv("TRACEY_LIB_PATH"))
            incs.push_back(p_env);
        if (const char *p_env = std::getenv("TRACEY_INCLUDE_PATH"))
            incs.push_back(p_env);
        if (verbose)
            std::print(stderr, "[preprocess] {}\n", path);
        auto t_pre = std::chrono::steady_clock::now();
        std::string content =
            preprocess(std::string(src.content()),
                       std::filesystem::path(path).parent_path().string(), incs, visited);
        if (verbose)
        {
            auto ms =
                std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t_pre)
                    .count();
            std::print(stderr, "[preprocess] done ({:.3f} ms)\n", ms);
        }

        // Lex
        tracey::lexer lx{content};
        std::size_t lex_errors = 0;
        const std::size_t max_lexer_errors = tracey::config::max_lexer_errors;
        // We need tokens for parser; but lexer is consumed by parser directly.
        // First check lexer errors by scanning? Parser will encounter error tokens.
        // We'll let parser handle; but driver previously reported lexer errors.
        // To preserve behavior, scan and store? Simpler: parser stops on error token.
        // We'll pre-scan to report lexer errors.
        if (verbose)
            std::print(stderr, "[lex] {}\n", path);
        auto t_lex = std::chrono::steady_clock::now();
        {
            tracey::lexer lx2{content};
            for (;;)
            {
                tracey::token t = lx2.next();
                if (t.is_error())
                {
                    tracey::print_error(t, path, stderr);
                    had_error = true;
                    if (++lex_errors >= max_lexer_errors)
                    {
                        std::print(stderr, "Error: {}: too many lexer errors, stopping\n", path);
                        break;
                    }
                    continue;
                }
                if (t.type == tracey::token_type::eof)
                    break;
            }
            if (lex_errors > 0)
                continue; // skip parse if lexer errors
        }
        if (verbose)
        {
            auto ms =
                std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t_lex)
                    .count();
            std::print(stderr, "[lex] done ({:.3f} ms)\n", ms);
        }

        // Parse
        if (verbose)
            std::print(stderr, "[parse] {}\n", path);
        auto t_parse = std::chrono::steady_clock::now();
        tracey::lexer lx3{content};
        tracey::parser p(lx3);
        tracey::program prog;
        if (!p.parse(prog))
        {
            for (auto &e : p.errors())
            {
                std::print(stderr, "{}:{}:{}: \033[1;31merror:\033[0m {}\n", path, e.line, e.column,
                           e.message);
            }
            had_error = true;
            if (verbose)
            {
                auto ms = std::chrono::duration<double, std::milli>(
                              std::chrono::steady_clock::now() - t_parse)
                              .count();
                std::print(stderr, "[parse] failed ({:.3f} ms)\n", ms);
            }
            continue;
        }
        if (verbose)
        {
            auto ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() -
                                                                t_parse)
                          .count();
            std::print(stderr, "[parse] done ({:.3f} ms)\n", ms);
        }

        // Semantic
        if (verbose)
            std::print(stderr, "[semantic] {}\n", path);
        auto t_sem = std::chrono::steady_clock::now();
        tracey::semantic_analyzer sem;
        if (!sem.analyze(prog))
        {
            for (auto &e : sem.errors())
            {
                std::print(stderr, "{}:{}:{}: \033[1;31merror:\033[0m {}\n", path, e.line, e.column,
                           e.message);
            }
            had_error = true;
            if (verbose)
            {
                auto ms = std::chrono::duration<double, std::milli>(
                              std::chrono::steady_clock::now() - t_sem)
                              .count();
                std::print(stderr, "[semantic] failed ({:.3f} ms)\n", ms);
            }
            continue;
        }
        if (verbose)
        {
            auto ms =
                std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t_sem)
                    .count();
            std::print(stderr, "[semantic] done ({:.3f} ms)\n", ms);
        }

        // Codegen
        if (verbose)
            std::print(stderr, "[codegen] {}\n", path);
        auto t_cg = std::chrono::steady_clock::now();
        std::ostringstream ss;
        bool emit_rt = (i + 1 == sources.size());
        codegen.generate(prog, path, ss, (int)i, emit_rt);
        generated += ss.str();
        if (verbose)
        {
            auto ms =
                std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t_cg)
                    .count();
            std::print(stderr, "[codegen] done ({:.3f} ms)\n", ms);
        }
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
        std::print(stderr, "Assembler error: {}\n", asm_result.error);
        return 1;
    }
    if (!tracey::write_elf_object(obj_path.string(), asm_result))
    {
        std::print(stderr, "Error: cannot write object file\n");
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
