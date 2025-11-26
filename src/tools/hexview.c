#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>

int main(int argc, char** argv) {
    if (argc < 2) {
        printf("Usage: hexview <file>\n");
        return 1;
    }

    FILE* f = fopen(argv[1], "rb");
    if (!f) {
        printf("Error opening file: %s\n", argv[1]);
        return 1;
    }

    uint8_t byte;
    int count = 0;
    printf("Hex dump of %s:\n", argv[1]);
    while (fread(&byte, 1, 1, f) == 1) {
        printf("%02X ", byte);
        count++;
        if (count % 16 == 0) printf("\n");
    }
    if (count % 16 != 0) printf("\n");
    fclose(f);
    return 0;
}

