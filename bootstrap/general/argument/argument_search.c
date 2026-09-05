#include <tracey/argument.h>
#include <string.h>

const char* arguments_find_define(const Arguments* args, const char* name) {
    if (!args || !name) return NULL;
    size_t name_len = strlen(name);
    for (size_t i = 0; i < args->define_count; i++) {
        const char* def = args->defines[i];
        if (strncmp(def, name, name_len) == 0 &&
            (def[name_len] == '\0' || def[name_len] == '=')) {
            return def;
        }
    }
    return NULL;
}

bool arguments_has_include_path(const Arguments* args, const char* path) {
    if (!args || !path) return false;
    for (size_t i = 0; i < args->include_path_count; i++) {
        if (strcmp(args->include_paths[i], path) == 0) {
            return true;
        }
    }
    return false;
}
