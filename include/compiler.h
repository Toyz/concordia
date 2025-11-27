#ifndef CND_COMPILER_H
#define CND_COMPILER_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// Compile a .cnd file to a .il file
// Returns 0 on success, non-zero on error
int cnd_compile_file(const char* in_path, const char* out_path, int json_output);

// Format a .cnd file
// If out_path is NULL, prints to stdout
int cnd_format_file(const char* in_path, const char* out_path);

#ifdef __cplusplus
}
#endif

#endif
