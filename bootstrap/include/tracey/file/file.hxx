#pragma once

#include <tracey/struct/file_struct.hxx>

int tracey_file_open(TraceyFile *file, const char *path);
void tracey_file_close(TraceyFile *file);
void tracey_debug_file_print(TraceyFile *file);
