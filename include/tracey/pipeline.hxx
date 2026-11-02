#pragma once

#include <tracey/ast.hxx>
#include <tracey/codegen.hxx>
#include <tracey/config.hxx>
#include <tracey/lexer.hxx>
#include <tracey/parser.hxx>
#include <tracey/preprocessor.hxx>
#include <tracey/semantic.hxx>
#include <tracey/source.hxx>

#include <cstddef>
#include <filesystem>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

namespace tracey
{

struct compile_options
{
    target_arch arch{target_arch::aarch64};
    bool verbose{false};
    std::vector<std::string> include_paths;
};

struct file_result
{
    std::string assembly;
    bool had_error{false};
};

class compile_pipeline
{
public:
    explicit compile_pipeline(const compile_options &opts);

    /*
     * process_file - Compile a single source file through all stages.
     *
     * src is the source file to compile. path is the file path used for
     * diagnostics. module_id is the zero-based index of this module.
     * emit_runtime_helpers indicates whether runtime helpers should be
     * emitted (typically true for the last module).
     *
     * Returns the generated assembly fragment and error status.
     */
    file_result process_file(const source &src, std::string_view path,
                            std::size_t module_id, bool emit_runtime_helpers);

private:
    compile_options opts_;
    code_generator codegen_;
};

} // namespace tracey