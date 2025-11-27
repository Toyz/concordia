#include <stdio.h>
#include <stdlib.h>
#include "../../include/compiler.h"

int cmd_fmt(int argc, char** argv) {
    if (argc < 3) {
        printf("Usage: cnd fmt <in.cnd> [out.cnd]\n");
        return 1;
    }

    const char* in_path = argv[2];
    const char* out_path = (argc >= 4) ? argv[3] : NULL;

    return cnd_format_file(in_path, out_path);
}
