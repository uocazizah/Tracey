#ifndef TRACEYC_INTERNAL_H
#define TRACEYC_INTERNAL_H

#include "tracey/argument.h"
#include "tracey/common.h"

/*
 * Internal helpers shared between the traceyc compilation units. These are
 * not part of the public library API and are only used by the traceyc tool.
 *
 * tracey_join_path is implemented by the preprocessor module (which traceyc
 * links against); the other helpers are implemented in this tool's own
 * compilation units.
 */

double tracey_now_ms(void);

char *tracey_make_temp_dir(void);

char *tracey_join_path(const char *dir, const char *name);

bool tracey_is_directory(const char *path);

const char **tracey_collect_include_paths(const struct tracey_args *args, size_t *p_count,
                                          char ***pp_owned);

struct tracey_source *tracey_load_sources(const struct tracey_args *args);

bool tracey_compile_inputs(const struct tracey_args *args, struct tracey_source *sources,
                          struct tracey_string *generated);

int tracey_emit_assembly_output(const struct tracey_args *args, struct tracey_string *generated);

int tracey_build_executable(const struct tracey_args *args, struct tracey_string *generated);

#endif /* TRACEYC_INTERNAL_H */