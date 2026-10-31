#include <tracey/elf.hxx>

#include <cstring>

namespace tracey::elf
{

void write_ehdr(std::ostream &o, const Ehdr &h)
{
    o.write(reinterpret_cast<const char *>(h.e_ident), 16);
    write_u16(o, h.e_type);
    write_u16(o, h.e_machine);
    write_u32(o, h.e_version);
    write_u64(o, h.e_entry);
    write_u64(o, h.e_phoff);
    write_u64(o, h.e_shoff);
    write_u32(o, h.e_flags);
    write_u16(o, h.e_ehsize);
    write_u16(o, h.e_phentsize);
    write_u16(o, h.e_phnum);
    write_u16(o, h.e_shentsize);
    write_u16(o, h.e_shnum);
    write_u16(o, h.e_shstrndx);
}
void write_shdr(std::ostream &o, const Shdr &h)
{
    write_u32(o, h.sh_name);
    write_u32(o, h.sh_type);
    write_u64(o, h.sh_flags);
    write_u64(o, h.sh_addr);
    write_u64(o, h.sh_offset);
    write_u64(o, h.sh_size);
    write_u32(o, h.sh_link);
    write_u32(o, h.sh_info);
    write_u64(o, h.sh_addralign);
    write_u64(o, h.sh_entsize);
}
void write_phdr(std::ostream &o, const Phdr &h)
{
    write_u32(o, h.p_type);
    write_u32(o, h.p_flags);
    write_u64(o, h.p_offset);
    write_u64(o, h.p_vaddr);
    write_u64(o, h.p_paddr);
    write_u64(o, h.p_filesz);
    write_u64(o, h.p_memsz);
    write_u64(o, h.p_align);
}
void write_sym(std::ostream &o, const Sym &s)
{
    write_u32(o, s.st_name);
    o.write(reinterpret_cast<const char *>(&s.st_info), 1);
    o.write(reinterpret_cast<const char *>(&s.st_other), 1);
    // padding to align? Sym is 24 bytes: name4 info1 other1 shndx2 value8 size8 =24. Need write
    // shndx as u16.
    write_u16(o, s.st_shndx);
    write_u64(o, s.st_value);
    write_u64(o, s.st_size);
}
void write_rela(std::ostream &o, const Rela &r)
{
    write_u64(o, r.r_offset);
    write_u64(o, r.r_info);
    write_i64(o, r.r_addend);
}

} // namespace tracey::elf
