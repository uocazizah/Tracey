#ifndef TRACEY_RUNTIME_HXX
#define TRACEY_RUNTIME_HXX

#include <tracey/codegen.hxx>

#include <iosfwd>

namespace tracey
{

void emit_runtime_helpers(std::ostream &out, target_arch arch);

} // namespace tracey

#endif /* TRACEY_RUNTIME_HXX */