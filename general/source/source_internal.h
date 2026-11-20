#ifndef TRACEY_SOURCE_INTERNAL_H
#define TRACEY_SOURCE_INTERNAL_H

#include "tracey/source.h"

#include "tracey/config.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*
 * tracey_source_init_empty - initialize a source snapshot to empty state.
 *
 * src must be non-NULL. No memory is allocated. The function is shared
 * between the core source helpers and the disk-reading implementation so
 * that every code path resets a snapshot consistently.
 */
void tracey_source_init_empty(struct tracey_source *src);

#endif /* TRACEY_SOURCE_INTERNAL_H */
