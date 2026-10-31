#ifndef TRACEY_LINKER_HXX
#define TRACEY_LINKER_HXX

#include <string>
#include <vector>

namespace tracey
{

bool link_elf_aarch64(const std::vector<std::string> &objects, const std::string &output);

} // namespace tracey

#endif
