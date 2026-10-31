#ifndef TRACEY_ASSEMBLER_HXX
#define TRACEY_ASSEMBLER_HXX

#include <cstdint>
#include <string>
#include <vector>

namespace tracey
{

struct asm_reloc
{
    std::size_t offset;
    std::string symbol;
    int type;
};
struct asm_symbol
{
    std::string name;
    std::size_t offset;
    bool global;
};

struct asm_result
{
    std::vector<std::uint8_t> text;
    std::vector<std::uint8_t> debug_line;
    std::vector<std::uint8_t> debug_info;
    std::vector<std::uint8_t> debug_abbrev;
    std::vector<asm_symbol> symbols;
    std::vector<asm_reloc> relocs;
    std::string error;
    bool ok{false};
};

asm_result assemble_aarch64(const std::string &source);

bool write_elf_object(const std::string &path, const asm_result &r);

} // namespace tracey

#endif
