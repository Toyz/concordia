#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <cJSON.h>
#include "concordia.h"
#include "compiler.h"
#include "cli_helpers.h" // Include the common helpers header

// --- Forward Declarations for CLI Commands ---
// These are now defined in separate .c files
extern int cmd_compile(int argc, char** argv);
extern int cmd_encode(int argc, char** argv);
extern int cmd_decode(int argc, char** argv);
extern int cmd_fmt(int argc, char** argv);


// --- main function for the cnd CLI tool ---
int main(int argc, char** argv) {
    setbuf(stdout, NULL); // Ensure prints appear immediately

    if (argc < 2) {
        printf("Concordia CLI\n");
        printf("Usage:\n");
        printf("  cnd compile <in.cnd> <out.il>\n");
        printf("  cnd fmt <in.cnd> [out.cnd]\n");
        printf("  cnd encode <schema.il> <in.json> <out.bin>\n");
        printf("  cnd decode <schema.il> <in.bin> <out.json>\n");
        return 1;
    }
    
    if (strcmp(argv[1], "compile") == 0) return cmd_compile(argc, argv);
    if (strcmp(argv[1], "fmt") == 0) return cmd_fmt(argc, argv);
    if (strcmp(argv[1], "encode") == 0) return cmd_encode(argc, argv);
    if (strcmp(argv[1], "decode") == 0) return cmd_decode(argc, argv);
    
    printf("Unknown command: %s\n", argv[1]);
    return 1;
}
