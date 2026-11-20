#ifndef TRACEY_LINKER_INTERNAL_H
#define TRACEY_LINKER_INTERNAL_H
#include "tracey/elf.h"
#include "tracey/linker.h"
#include <inttypes.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define LINK_EHDR_SIZE ((uint64_t)64)
#define LINK_PHDR_SIZE ((uint64_t)56)
#define LINK_SECTION_HEADER_SIZE ((uint64_t)64)
#define LINK_SYMBOL_ENTRY_SIZE ((uint64_t)24)
#define LINK_RELOCA_ENTRY_SIZE ((uint64_t)24)
#define LINK_TEXT_FILE_OFFSET ((uint64_t)0x1000)
#define LINK_BASE_VA ((uint64_t)0x400000)
#define LINK_CODE_BASE_VA (LINK_BASE_VA + LINK_TEXT_FILE_OFFSET)
#define LINK_PROGRAM_ALIGNMENT ((uint64_t)0x1000)
#define LINK_INDEX_TEXT ((uint32_t)1)
#define LINK_INDEX_SYMTAB ((uint32_t)2)
#define LINK_INDEX_STRTAB ((uint32_t)3)
#define LINK_INDEX_SHSTRTAB ((uint32_t)4)
#define LINK_INSN_ALIGNMENT ((uint64_t)4)
#define LINK_SECTION_ALIGNMENT ((uint64_t)8)
#define LINK_TEXT_SHADDR_ALIGNMENT ((uint64_t)16)
#define LINK_STARTUP_STUB_SIZE ((uint64_t)12)
#define LINK_STARTUP_MOV_X8 ((uint32_t)0xD2800BA8u)
#define LINK_STARTUP_SVC0 ((uint32_t)0xD4000001u)
#define LINK_BL_OPCODE ((uint32_t)0x94000000u)
#define LINK_R_AARCH64_CONDBR19 ((uint32_t)280)
#define LINK_R_AARCH64_JUMP26 ((uint32_t)282)
#define LINK_R_AARCH64_CALL26 ((uint32_t)283)
#define LINK_JUMP26_MASK ((uint32_t)0x03FFFFFFu)
#define LINK_JUMP26_MIN_DELTA ((int64_t)-0x08000000)
#define LINK_JUMP26_MAX_DELTA ((int64_t)0x07FFFFFC)
#define LINK_CONDBR19_MASK ((uint32_t)0x0007FFFFu)
#define LINK_CONDBR19_FIELD ((uint32_t)0x00FFFFE0u)
#define LINK_CONDBR19_SHIFT ((uint32_t)5)
#define LINK_CONDBR19_MIN_DELTA ((int64_t)-0x00100000)
#define LINK_CONDBR19_MAX_DELTA ((int64_t)0x000FFFFC)
struct link_buffer { unsigned char *data; size_t size; size_t capacity; };
struct link_reader { const unsigned char *data; size_t size; };
struct link_section { const unsigned char *data; size_t size; };
struct link_section_header { uint32_t name; uint32_t type; uint64_t offset; uint64_t size; uint32_t link; };
struct link_symbol { char *name; uint64_t value; bool is_global; bool is_defined; };
struct link_reloc { uint64_t offset; uint32_t type; char *symbol; };
struct link_object {
    unsigned char *text; size_t text_size;
    struct link_symbol *symbols; size_t symbol_count; size_t symbol_capacity;
    struct link_reloc *relocs; size_t reloc_count; size_t reloc_capacity;
    unsigned char *debug_line; size_t debug_line_size;
    unsigned char *debug_info; size_t debug_info_size;
    unsigned char *debug_abbrev; size_t debug_abbrev_size;
};
struct link_resolved_symbol { const char *name; uint64_t address; bool is_global; };
struct link_symbol_table { struct link_resolved_symbol *items; size_t count; size_t capacity; };
struct link_output_symbol { const char *name; uint64_t address; bool is_global; };
void link_diagnostic(const char *fmt, ...);
uint64_t link_align_up(uint64_t value, uint64_t alignment);
void buffer_init(struct link_buffer *buffer);
void buffer_free(struct link_buffer *buffer);
bool buffer_reserve(struct link_buffer *buffer, size_t extra);
bool buffer_append(struct link_buffer *buffer, const void *data, size_t size);
bool buffer_append_zeros(struct link_buffer *buffer, size_t count);
bool link_read_file(const char *path, struct link_buffer *out);
void reader_init(struct link_reader *reader, const unsigned char *data, size_t size);
bool reader_range_ok(const struct link_reader *reader, uint64_t offset, uint64_t length);
bool reader_u16(const struct link_reader *reader, uint64_t offset, uint16_t *out);
bool reader_u32(const struct link_reader *reader, uint64_t offset, uint32_t *out);
bool reader_u64(const struct link_reader *reader, uint64_t offset, uint64_t *out);
void store_u32(unsigned char *p, uint32_t value);
uint32_t load_u32(const unsigned char *p);
char *link_strdup(const char *text);
bool section_from_header(const struct link_reader *reader, const struct link_section_header *header, struct link_section *out);
bool section_string(const struct link_section *table, uint32_t index, const char **out);
bool section_name_is(const struct link_section *shstrtab, uint32_t name_index, const char *literal, bool *p_match);
void link_object_free(struct link_object *object);
bool link_object_take_debug(unsigned char **p_data, size_t *p_size, const struct link_section *section);
bool grow_array(void **p_items, size_t *p_capacity, size_t needed, size_t item_size);
bool link_write_zeros(FILE *file, uint64_t count);
bool link_write_padding(FILE *file, uint64_t cursor, uint64_t target);
bool link_write_bytes(FILE *file, const void *data, size_t size);
bool link_object_read(const char *path, struct link_object *object);
void link_symbol_table_init(struct link_symbol_table *table);
void link_symbol_table_free(struct link_symbol_table *table);
size_t link_symbol_table_find(const struct link_symbol_table *table, const char *name);
bool link_symbol_table_add(struct link_symbol_table *table, const char *name, uint64_t address, bool is_global);
int link_output_symbol_compare(const void *lhs, const void *rhs);
bool link_concatenate_text(struct link_object *loaded, size_t object_count, uint64_t *bases, struct link_buffer *text);
bool link_build_symbol_table(struct link_object *loaded, size_t object_count, const uint64_t *bases, struct link_symbol_table *table);
bool link_patch_relocations(struct link_object *loaded, size_t object_count, const char *const *objects, const uint64_t *bases, struct link_symbol_table *table, struct link_buffer *text);
bool link_emit_startup_stub(struct link_symbol_table *table, struct link_buffer *text);
bool link_write_output(const char *output, struct link_buffer *text, struct link_symbol_table *table,
                      const unsigned char *debug_line, size_t debug_line_size,
                      const unsigned char *debug_info, size_t debug_info_size,
                      const unsigned char *debug_abbrev, size_t debug_abbrev_size);
#endif
