#include "../../include/cli_helpers.h"

// Defined in src/compiler/cndc.c
extern int cnd_compile_file(const char* in_path, const char* out_path, int json_output);

int cmd_compile(int argc, char** argv) {
    if (argc < 4) {
        printf("Usage: cnd compile <input.cnd> <output.il> [--json]\n");
        return 1;
    }
    
    int json_output = 0;
    if (argc > 4 && strcmp(argv[4], "--json") == 0) {
        json_output = 1;
    }
    
    return cnd_compile_file(argv[2], argv[3], json_output);
}

