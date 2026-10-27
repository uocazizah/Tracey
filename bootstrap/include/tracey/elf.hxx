#ifndef TRACEY_ELF_HXX
#define TRACEY_ELF_HXX

#include <cstddef>
#include <cstdint>
#include <fstream>
#include <string>
#include <vector>

namespace tracey::elf {

struct Ehdr {
    unsigned char e_ident[16]{};
    std::uint16_t e_type{0};
    std::uint16_t e_machine{0};
    std::uint32_t e_version{1};
    std::uint64_t e_entry{0};
    std::uint64_t e_phoff{0};
    std::uint64_t e_shoff{0};
    std::uint32_t e_flags{0};
    std::uint16_t e_ehsize{64};
    std::uint16_t e_phentsize{56};
    std::uint16_t e_phnum{0};
    std::uint16_t e_shentsize{64};
    std::uint16_t e_shnum{0};
    std::uint16_t e_shstrndx{0};
};

struct Shdr {
    std::uint32_t sh_name{0};
    std::uint32_t sh_type{0};
    std::uint64_t sh_flags{0};
    std::uint64_t sh_addr{0};
    std::uint64_t sh_offset{0};
    std::uint64_t sh_size{0};
    std::uint32_t sh_link{0};
    std::uint32_t sh_info{0};
    std::uint64_t sh_addralign{1};
    std::uint64_t sh_entsize{0};
};

struct Phdr {
    std::uint32_t p_type{0};
    std::uint32_t p_flags{0};
    std::uint64_t p_offset{0};
    std::uint64_t p_vaddr{0};
    std::uint64_t p_paddr{0};
    std::uint64_t p_filesz{0};
    std::uint64_t p_memsz{0};
    std::uint64_t p_align{0};
};

struct Sym {
    std::uint32_t st_name{0};
    unsigned char st_info{0};
    unsigned char st_other{0};
    std::uint16_t st_shndx{0};
    std::uint64_t st_value{0};
    std::uint64_t st_size{0};
};

struct Rela {
    std::uint64_t r_offset{0};
    std::uint64_t r_info{0};
    std::int64_t r_addend{0};
};

// ELF constants
constexpr std::uint16_t ET_REL = 1;
constexpr std::uint16_t ET_EXEC = 2;
constexpr std::uint16_t EM_AARCH64 = 183;
constexpr std::uint32_t SHT_NULL = 0;
constexpr std::uint32_t SHT_PROGBITS = 1;
constexpr std::uint32_t SHT_SYMTAB = 2;
constexpr std::uint32_t SHT_STRTAB = 3;
constexpr std::uint32_t SHT_RELA = 4;
constexpr std::uint64_t SHF_ALLOC = 0x2;
constexpr std::uint64_t SHF_EXECINSTR = 0x4;
constexpr std::uint32_t SHN_UNDEF = 0;
constexpr std::uint8_t STB_GLOBAL = 1;
constexpr std::uint8_t STT_FUNC = 2;
constexpr std::uint8_t STT_SECTION = 3;
constexpr std::uint32_t PT_LOAD = 1;
constexpr std::uint32_t PT_INTERP = 3;
constexpr std::uint64_t PF_X = 1;
constexpr std::uint64_t PF_W = 2;
constexpr std::uint64_t PF_R = 4;
constexpr std::uint32_t R_AARCH64_CALL26 = 283;
constexpr std::uint32_t R_AARCH64_JUMP26 = 282;

// helpers
inline void write_u16(std::ostream& o, std::uint16_t v){ o.write(reinterpret_cast<const char*>(&v),2);}
inline void write_u32(std::ostream& o, std::uint32_t v){ o.write(reinterpret_cast<const char*>(&v),4);}
inline void write_u64(std::ostream& o, std::uint64_t v){ o.write(reinterpret_cast<const char*>(&v),8);}
inline void write_i64(std::ostream& o, std::int64_t v){ o.write(reinterpret_cast<const char*>(&v),8);}

void write_ehdr(std::ostream& o, const Ehdr& h);
void write_shdr(std::ostream& o, const Shdr& h);
void write_phdr(std::ostream& o, const Phdr& h);
void write_sym(std::ostream& o, const Sym& s);
void write_rela(std::ostream& o, const Rela& r);

} // namespace tracey::elf

#endif
