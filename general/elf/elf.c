#include "tracey/elf.h"

/*
 * Scalar writers.
 *
 * Every writer emits the value in little-endian byte order regardless of
 * host endianness, so the produced objects are valid AArch64 ELF64 files on
 * any build machine. Write errors are not reported here; callers check
 * ferror() once after emitting a complete structure.
 */
void tracey_elf_write_u16(FILE *o, uint16_t v)
{
    unsigned char b[2];
    b[0] = (unsigned char)(v & 0xFFu);
    b[1] = (unsigned char)((v >> 8) & 0xFFu);
    fwrite(b, 1, 2, o);
}

void tracey_elf_write_u32(FILE *o, uint32_t v)
{
    unsigned char b[4];
    b[0] = (unsigned char)(v & 0xFFu);
    b[1] = (unsigned char)((v >> 8) & 0xFFu);
    b[2] = (unsigned char)((v >> 16) & 0xFFu);
    b[3] = (unsigned char)((v >> 24) & 0xFFu);
    fwrite(b, 1, 4, o);
}

void tracey_elf_write_u64(FILE *o, uint64_t v)
{
    unsigned char b[8];
    int i;
    for (i = 0; i < 8; i++)
        b[i] = (unsigned char)((v >> (8 * i)) & 0xFFu);
    fwrite(b, 1, 8, o);
}

void tracey_elf_write_i64(FILE *o, int64_t v)
{
    tracey_elf_write_u64(o, (uint64_t)v);
}

void tracey_elf_write_ehdr(FILE *o, const struct tracey_elf_ehdr *h)
{
    if (o == NULL || h == NULL)
        return;
    fwrite(h->e_ident, 1, 16, o);
    tracey_elf_write_u16(o, h->e_type);
    tracey_elf_write_u16(o, h->e_machine);
    tracey_elf_write_u32(o, h->e_version);
    tracey_elf_write_u64(o, h->e_entry);
    tracey_elf_write_u64(o, h->e_phoff);
    tracey_elf_write_u64(o, h->e_shoff);
    tracey_elf_write_u32(o, h->e_flags);
    tracey_elf_write_u16(o, h->e_ehsize);
    tracey_elf_write_u16(o, h->e_phentsize);
    tracey_elf_write_u16(o, h->e_phnum);
    tracey_elf_write_u16(o, h->e_shentsize);
    tracey_elf_write_u16(o, h->e_shnum);
    tracey_elf_write_u16(o, h->e_shstrndx);
}

void tracey_elf_write_shdr(FILE *o, const struct tracey_elf_shdr *h)
{
    if (o == NULL || h == NULL)
        return;
    tracey_elf_write_u32(o, h->sh_name);
    tracey_elf_write_u32(o, h->sh_type);
    tracey_elf_write_u64(o, h->sh_flags);
    tracey_elf_write_u64(o, h->sh_addr);
    tracey_elf_write_u64(o, h->sh_offset);
    tracey_elf_write_u64(o, h->sh_size);
    tracey_elf_write_u32(o, h->sh_link);
    tracey_elf_write_u32(o, h->sh_info);
    tracey_elf_write_u64(o, h->sh_addralign);
    tracey_elf_write_u64(o, h->sh_entsize);
}

void tracey_elf_write_phdr(FILE *o, const struct tracey_elf_phdr *h)
{
    if (o == NULL || h == NULL)
        return;
    tracey_elf_write_u32(o, h->p_type);
    tracey_elf_write_u32(o, h->p_flags);
    tracey_elf_write_u64(o, h->p_offset);
    tracey_elf_write_u64(o, h->p_vaddr);
    tracey_elf_write_u64(o, h->p_paddr);
    tracey_elf_write_u64(o, h->p_filesz);
    tracey_elf_write_u64(o, h->p_memsz);
    tracey_elf_write_u64(o, h->p_align);
}

void tracey_elf_write_sym(FILE *o, const struct tracey_elf_sym *s)
{
    if (o == NULL || s == NULL)
        return;
    tracey_elf_write_u32(o, s->st_name);
    fputc(s->st_info, o);
    fputc(s->st_other, o);
    tracey_elf_write_u16(o, s->st_shndx);
    tracey_elf_write_u64(o, s->st_value);
    tracey_elf_write_u64(o, s->st_size);
}

void tracey_elf_write_rela(FILE *o, const struct tracey_elf_rela *r)
{
    if (o == NULL || r == NULL)
        return;
    tracey_elf_write_u64(o, r->r_offset);
    tracey_elf_write_u64(o, r->r_info);
    tracey_elf_write_i64(o, r->r_addend);
}
