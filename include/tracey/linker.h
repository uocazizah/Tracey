#ifndef TRACEY_LINKER_H
#define TRACEY_LINKER_H

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * tracey_link_elf_aarch64 - link relocatable AArch64 objects into an
 * executable image.
 *
 * objects points to an array of object_count NUL-terminated file paths. Each
 * file must be an ELF64 little-endian relocatable object (ET_REL) for
 * EM_AARCH64 as produced by the Tracey assembler. objects[i] is only read
 * during the call; the caller keeps ownership of the path strings.
 *
 * output names the file that receives the statically linked ELF64 executable
 * (ET_EXEC). The file is created or truncated. The linker writes a single
 * PT_LOAD segment, one .text section combining the input code, a symbol table
 * and a section header table. When the inputs do not define _start, a
 * twelve-byte startup stub that branches to main and performs the Linux
 * exit(2) system call is prepended to .text.
 *
 * The function returns true when the executable has been written completely.
 * It returns false on any error, after printing a diagnostic to stderr:
 * unreadable input, malformed ELF data, unresolved or out-of-range
 * relocations, allocation failure and file write failure are all reported.
 * No heap memory remains allocated after the call in either case.
 */
bool tracey_link_elf_aarch64(const char *const *objects, size_t object_count, const char *output);

#ifdef __cplusplus
}
#endif

#endif /* TRACEY_LINKER_H */
