#ifndef TRACEY_ASSEMBLER_H
#define TRACEY_ASSEMBLER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * AArch64 assembler interface.
 *
 * tracey_assemble_aarch64() translates the restricted assembly dialect
 * emitted by the Tracey code generator into machine code, DWARF4 line
 * information and the symbol/relocation data required to build an ELF64
 * relocatable object. tracey_write_elf_object() serializes such a result.
 *
 * Ownership rules for struct tracey_asm_result:
 *
 *   - every pointer in the structure is either NULL or points to a
 *     heap allocation obtained with malloc;
 *   - the caller owns the whole structure and must release it with
 *     tracey_asm_result_free(), including when ok is false, because a
 *     partially built result may still hold allocations;
 *   - symbol names and relocation symbol names are individually allocated
 *     NUL-terminated strings and are never shared with the assembler's
 *     internal tables.
 *
 * All *_len / *_count fields describe the number of valid elements in the
 * matching array; the arrays carry no capacity information.
 */

/*
 * AArch64 ELF relocation attached to a byte offset in .text.
 *
 * offset is the byte offset of the instruction that must be patched.
 * symbol names the target and is an owned NUL-terminated string.
 * type is the ELF relocation type number (R_AARCH64_*).
 */
struct tracey_asm_reloc
{
    size_t offset;
    char *symbol;
    int type;
};

/*
 * Symbol table entry produced by the assembler.
 *
 * name is an owned NUL-terminated string. offset is the byte offset within
 * the flat output blob (the value that becomes st_value for the .text
 * section). global is true when the name appeared in a .globl directive.
 */
struct tracey_asm_symbol
{
    char *name;
    size_t offset;
    bool global;
};

/*
 * Result of one assembly run.
 *
 * text holds the encoded instructions and .asciz data. The debug_* buffers
 * hold the DWARF4 sections (.debug_line, .debug_info, .debug_abbrev).
 * symbols and relocs describe the label table and the pending relocations.
 *
 * error always holds a NUL-terminated diagnostic; it is empty when ok is
 * true. ok is set to true only when the complete source was assembled.
 */
struct tracey_asm_result
{
    uint8_t *text;
    size_t text_len;
    uint8_t *debug_line;
    size_t debug_line_len;
    uint8_t *debug_info;
    size_t debug_info_len;
    uint8_t *debug_abbrev;
    size_t debug_abbrev_len;
    struct tracey_asm_symbol *symbols;
    size_t symbol_count;
    struct tracey_asm_reloc *relocs;
    size_t reloc_count;
    char error[512];
    bool ok;
};

/*
 * tracey_assemble_aarch64 - assemble one AArch64 source buffer.
 *
 * source points to source_len bytes of assembly text. The buffer does not
 * need to be NUL-terminated and is not modified or retained. source may be
 * NULL when source_len is zero, which yields an empty module.
 *
 * The function never aborts on invalid input: encoding errors are reported
 * through the error field of the returned structure with ok set to false.
 * The returned structure owns all of its allocations and must be released
 * with tracey_asm_result_free().
 */
struct tracey_asm_result tracey_assemble_aarch64(const char *source, size_t source_len);

/*
 * tracey_asm_result_free - release every allocation owned by r.
 *
 * r may be NULL. The structure is cleared afterwards so a second call is
 * harmless.
 */
void tracey_asm_result_free(struct tracey_asm_result *r);

/*
 * tracey_write_elf_object - write r as an ELF64 AArch64 relocatable object.
 *
 * path names the output file, which is created or truncated. r must be a
 * result produced by tracey_assemble_aarch64(); it is not modified.
 *
 * Returns true when the complete object was written and flushed. On failure
 * a diagnostic describing the reason is printed to stderr and false is
 * returned; a partially written file may be left behind in that case.
 */
bool tracey_write_elf_object(const char *path, const struct tracey_asm_result *r);

#ifdef __cplusplus
}
#endif

#endif /* TRACEY_ASSEMBLER_H */
