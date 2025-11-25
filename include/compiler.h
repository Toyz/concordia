#ifndef CND_COMPILER_H
#define CND_COMPILER_H

#include <stdbool.h>

// Compiles a .cnd file at in_path to a .il file at out_path.
// Returns 0 on success, non-zero on error.
int cnd_compile_file(const char* in_path, const char* out_path);

#endif
