#ifndef TRACEY_CODEGEN_HXX
#define TRACEY_CODEGEN_HXX

#include <tracey/ast.hxx>

#include <iosfwd>
#include <string>

namespace tracey
{

enum class target_arch
{
    aarch64,
    x86_64
};

class code_generator
{
  public:
    explicit code_generator(target_arch arch = target_arch::aarch64);

    void generate(const program &p, const std::string &source_name, std::ostream &out,
                  int module_id = 0, bool emit_runtime_helpers = true);

    [[nodiscard]] target_arch arch() const noexcept
    {
        return arch_;
    }

  private:
    target_arch arch_;
};

} // namespace tracey

#endif
