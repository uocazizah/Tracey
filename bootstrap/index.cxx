#include <tracey/argument.hxx>
#include <tracey/config.hxx>
#include <tracey/lexer.hxx>
#include <tracey/source.hxx>

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

    /* Read input files (RAII: cleaned up automatically on every path) */
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

    /* Debug output */
    if (args.has_verbose()) {
        args.print_debug(stdout);

        for (std::size_t i = 0; i < sources.size(); i++) {
            if (i > 0) std::print(stdout, "\n");
            sources[i].print_debug(stdout);
        }
    }

    /* Lexical analysis */
    bool had_lexer_error = false;
    for (std::size_t i = 0; i < sources.size(); i++) {
        const std::string& path = inputs[i];
        const tracey::source& src = sources[i];

        std::print(stdout, "\n=== Lexical Analysis: {} ===\n", path);

        tracey::lexer lx{src.content()};

        tracey::token token;
        do {
            token = lx.next();
            token.print_debug(stdout);
        } while (token.type != tracey::token_type::eof && token.type != tracey::token_type::error_);

        if (token.type == tracey::token_type::error_) {
            std::print(stderr, "Lexer error at line {}, column {}\n", token.line, token.column);
            had_lexer_error = true;
        }
    }

    return had_lexer_error ? 1 : 0;
}
