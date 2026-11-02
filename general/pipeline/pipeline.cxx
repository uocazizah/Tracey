#include <tracey/pipeline.hxx>

#include <tracey/argument.hxx>

#include <chrono>
#include <iostream>
#include <print>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace tracey
{

compile_pipeline::compile_pipeline(const compile_options &opts)
    : opts_(opts), codegen_(opts.arch)
{
}

file_result compile_pipeline::process_file(const source &src, std::string_view path,
                                          std::size_t module_id, bool emit_runtime_helpers)
{
    file_result result;
    const bool verbose = opts_.verbose;

    // Preprocess
    tracey::preprocessor_config pre_config;
    pre_config.include_paths = opts_.include_paths;
    pre_config.base_dir = std::filesystem::path(path).parent_path();

    if (verbose)
        std::print(stderr, "[preprocess] {}\n", path);
    auto t_pre = std::chrono::steady_clock::now();
    auto pre_result = tracey::preprocess(src.content(), pre_config);
    if (pre_result.had_error)
    {
        result.had_error = true;
        return result;
    }
    std::string content = std::move(pre_result.content);
    if (verbose)
    {
        auto ms =
            std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t_pre)
                .count();
        std::print(stderr, "[preprocess] done ({:.3f} ms)\n", ms);
    }

    // Lex - scan for lexer errors
    if (verbose)
        std::print(stderr, "[lex] {}\n", path);
    auto t_lex = std::chrono::steady_clock::now();
    {
        tracey::lexer lx2{content};
        std::size_t lex_errors = 0;
        const std::size_t max_lexer_errors = tracey::config::max_lexer_errors;
        for (;;)
        {
            tracey::token t = lx2.next();
            if (t.is_error())
            {
                tracey::print_error(t, std::string(path), stderr);
                result.had_error = true;
                if (++lex_errors >= max_lexer_errors)
                {
                    std::print(stderr, "Error: {}: too many lexer errors, stopping\n", path);
                    std::print(stderr, "\033[1;34mnote:\033[0m fix the reported lexer errors and recompile\n");
                    break;
                }
                continue;
            }
            if (t.type == tracey::token_type::eof)
                break;
        }
        if (lex_errors > 0)
        {
            if (verbose)
            {
                auto ms = std::chrono::duration<double, std::milli>(
                              std::chrono::steady_clock::now() - t_lex)
                              .count();
                std::print(stderr, "[lex] done ({:.3f} ms)\n", ms);
            }
            return result;
        }
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
            std::print(stderr, "{}:{}:{}: \033[1;34mnote:\033[0m review the syntax and types at this location\n", path, e.line, e.column);
        }
        result.had_error = true;
        if (verbose)
        {
            auto ms = std::chrono::duration<double, std::milli>(
                          std::chrono::steady_clock::now() - t_parse)
                          .count();
            std::print(stderr, "[parse] failed ({:.3f} ms)\n", ms);
        }
        return result;
    }
    if (verbose)
    {
        auto ms = std::chrono::duration<double, std::milli>(
                      std::chrono::steady_clock::now() - t_parse)
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
            std::print(stderr, "{}:{}:{}: \033[1;34mnote:\033[0m review the syntax and types at this location\n", path, e.line, e.column);
        }
        result.had_error = true;
        if (verbose)
        {
            auto ms = std::chrono::duration<double, std::milli>(
                          std::chrono::steady_clock::now() - t_sem)
                          .count();
            std::print(stderr, "[semantic] failed ({:.3f} ms)\n", ms);
        }
        return result;
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
    codegen_.generate(prog, std::string(path), ss, (int)module_id, emit_runtime_helpers);
    result.assembly = ss.str();
    if (verbose)
    {
        auto ms =
            std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t_cg)
                .count();
        std::print(stderr, "[codegen] done ({:.3f} ms)\n", ms);
    }

    return result;
}

} // namespace tracey