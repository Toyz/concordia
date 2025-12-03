#include "cli_helpers.h"

// Defined in src/compiler/cndc.c
extern int cnd_compile_file(const char* in_path, const char* out_path, int json_output, int verbose);

int cmd_compile(int argc, char** argv) {
    if (argc < 4) {
        printf("Usage: cnd compile <input.cnd> <output.il> [--json] [--verbose]\n");
        return 1;
    }
    
    int json_output = 0;
    int verbose = 0;
    
    for (int i = 4; i < argc; i++) {
        if (strcmp(argv[i], "--json") == 0) {
            json_output = 1;
        } else if (strcmp(argv[i], "--verbose") == 0 || strcmp(argv[i], "-v") == 0) {
            verbose = 1;
        }
    }
    
    return cnd_compile_file(argv[2], argv[3], json_output, verbose);
}

