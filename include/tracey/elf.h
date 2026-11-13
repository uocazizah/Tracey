#ifndef TRACEY_ELF_H
#define TRACEY_ELF_H

#include <stdint.h>
#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * ELF64 (AArch64, little-endian) structure definitions used by the
 * Tracey assembler and linker.
 *
 * The structures mirror the on-disk layout of the ELF64 specification.
 * Fields are written explicitly, in order, by the write_* helpers below,
 * so the compiler is never relied upon to lay these structs out byte for
 * byte. Callers may therefore treat the structs as plain aggregates.
 */

struct tracey_elf_ehdr
{
    unsigned char e_ident[16];
    uint16_t e_type;
    uint16_t e_machine;
    uint32_t e_version;
    uint64_t e_entry;
    uint64_t e_phoff;
    uint64_t e_shoff;
    uint32_t e_flags;
    uint16_t e_ehsize;
    uint16_t e_phentsize;
    uint16_t e_phnum;
    uint16_t e_shentsize;
    uint16_t e_shnum;
    uint16_t e_shstrndx;
};

struct tracey_elf_shdr
{
    uint32_t sh_name;
    uint32_t sh_type;
    uint64_t sh_flags;
    uint64_t sh_addr;
    uint64_t sh_offset;
    uint64_t sh_size;
    uint32_t sh_link;
    uint32_t sh_info;
    uint64_t sh_addralign;
    uint64_t sh_entsize;
};

struct tracey_elf_phdr
{
    uint32_t p_type;
    uint32_t p_flags;
    uint64_t p_offset;
    uint64_t p_vaddr;
    uint64_t p_paddr;
    uint64_t p_filesz;
    uint64_t p_memsz;
    uint64_t p_align;
};

struct tracey_elf_sym
{
    uint32_t st_name;
    unsigned char st_info;
    unsigned char st_other;
    uint16_t st_shndx;
    uint64_t st_value;
    uint64_t st_size;
};

struct tracey_elf_rela
{
    uint64_t r_offset;
    uint64_t r_info;
    int64_t r_addend;
};

/* ELF constants used by the assembler and linker. */
#define TRACEY_ELF_ET_REL ((uint16_t)1)
#define TRACEY_ELF_ET_EXEC ((uint16_t)2)
#define TRACEY_ELF_EM_AARCH64 ((uint16_t)183)
#define TRACEY_ELF_SHT_NULL ((uint32_t)0)
#define TRACEY_ELF_SHT_PROGBITS ((uint32_t)1)
#define TRACEY_ELF_SHT_SYMTAB ((uint32_t)2)
#define TRACEY_ELF_SHT_STRTAB ((uint32_t)3)
#define TRACEY_ELF_SHT_RELA ((uint32_t)4)
#define TRACEY_ELF_SHF_ALLOC ((uint64_t)0x2)
#define TRACEY_ELF_SHF_EXECINSTR ((uint64_t)0x4)
#define TRACEY_ELF_SHN_UNDEF ((uint16_t)0)
#define TRACEY_ELF_STB_LOCAL ((unsigned char)0)
#define TRACEY_ELF_STB_GLOBAL ((unsigned char)1)
#define TRACEY_ELF_STT_FUNC ((unsigned char)2)
#define TRACEY_ELF_STT_SECTION ((unsigned char)3)
#define TRACEY_ELF_PT_LOAD ((uint32_t)1)
#define TRACEY_ELF_PT_INTERP ((uint32_t)3)
#define TRACEY_ELF_PF_X ((uint32_t)1)
#define TRACEY_ELF_PF_W ((uint32_t)2)
#define TRACEY_ELF_PF_R ((uint32_t)4)
#define TRACEY_ELF_R_AARCH64_CALL26 ((uint32_t)283)
#define TRACEY_ELF_R_AARCH64_JUMP26 ((uint32_t)282)

/* Little-endian scalar writers. Truncated writes are ignored on purpose:
 * the callers check ferror() on the stream after a structure is emitted. */
void tracey_elf_write_u16(FILE *o, uint16_t v);
void tracey_elf_write_u32(FILE *o, uint32_t v);
void tracey_elf_write_u64(FILE *o, uint64_t v);
void tracey_elf_write_i64(FILE *o, int64_t v);

void tracey_elf_write_ehdr(FILE *o, const struct tracey_elf_ehdr *h);
void tracey_elf_write_shdr(FILE *o, const struct tracey_elf_shdr *h);
void tracey_elf_write_phdr(FILE *o, const struct tracey_elf_phdr *h);
void tracey_elf_write_sym(FILE *o, const struct tracey_elf_sym *s);
void tracey_elf_write_rela(FILE *o, const struct tracey_elf_rela *r);

#ifdef __cplusplus
}
#endif

#endif /* TRACEY_ELF_H */
