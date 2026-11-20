#include "tracey/ast.h"

#include "ast_internal.h"

int tracey_program_add_struct(struct tracey_program *program, const char *name)
{
    struct tracey_struct_def *def;

    if (program == NULL || name == NULL)
        return -1;
    if (program->struct_count == program->struct_cap)
    {
        size_t cap = program->struct_cap ? program->struct_cap * 2 : 8;
        struct tracey_struct_def *p =
            (struct tracey_struct_def *)realloc(program->structs, cap * sizeof(*p));
        if (p == NULL)
            return -1;
        memset(p + program->struct_cap, 0, (cap - program->struct_cap) * sizeof(*p));
        program->structs = p;
        program->struct_cap = cap;
    }
    def = &program->structs[program->struct_count];
    def->name = strdup(name);
    if (def->name == NULL)
        return -1;
    def->members = NULL;
    def->member_count = 0;
    def->member_cap = 0;
    def->size = 0;
    def->alignment = 1;
    return (int)program->struct_count++;
}

struct tracey_struct_def *tracey_program_find_struct(const struct tracey_program *program,
                                                     const char *name)
{
    size_t i;

    if (program == NULL || name == NULL)
        return NULL;
    for (i = 0; i < program->struct_count; i++)
    {
        if (program->structs[i].name != NULL && strcmp(program->structs[i].name, name) == 0)
            return (struct tracey_struct_def *)&program->structs[i];
    }
    return NULL;
}

int tracey_program_find_struct_id(const struct tracey_program *program, const char *name)
{
    size_t i;

    if (program == NULL || name == NULL)
        return -1;
    for (i = 0; i < program->struct_count; i++)
    {
        if (program->structs[i].name != NULL && strcmp(program->structs[i].name, name) == 0)
            return (int)i;
    }
    return -1;
}

bool tracey_struct_add_member(struct tracey_struct_def *def, const char *name,
                              enum tracey_type_kind type, int struct_id,
                               size_t size, size_t alignment)
{
    size_t i;

    if (def == NULL || name == NULL || size == 0 || alignment == 0)
        return false;
    for (i = 0; i < def->member_count; i++)
    {
        if (def->members[i].name != NULL && strcmp(def->members[i].name, name) == 0)
            return false;
    }
    if (def->member_count == def->member_cap)
    {
        size_t cap = def->member_cap ? def->member_cap * 2 : 8;
        struct tracey_struct_member *p =
            (struct tracey_struct_member *)realloc(def->members, cap * sizeof(*p));
        if (p == NULL)
            return false;
        def->members = p;
        def->member_cap = cap;
    }
    def->members[def->member_count].type = type;
    def->members[def->member_count].struct_id = struct_id;
    def->members[def->member_count].name = strdup(name);
    if (def->members[def->member_count].name == NULL)
        return false;
    {
        size_t offset = def->size;
        if (offset % alignment != 0)
            offset += alignment - (offset % alignment);
        def->members[def->member_count].offset = offset;
        def->member_count++;
        if (alignment > def->alignment)
            def->alignment = alignment;
        if (def->alignment == 0)
            def->alignment = 1;
        def->size = offset + size;
        if (def->size % def->alignment != 0)
            def->size += def->alignment - (def->size % def->alignment);
    }
    return true;
}
