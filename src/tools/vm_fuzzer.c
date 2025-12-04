#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <stdint.h>
#include "concordia.h"

// Simple pseudo-random generator for reproducibility if needed
static uint32_t rand_state = 123456789;
static uint32_t xorshift32(void) {
    uint32_t x = rand_state;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    rand_state = x;
    return x;
}

static void fill_random(uint8_t* buf, size_t len) {
    for (size_t i = 0; i < len; i++) {
        buf[i] = (uint8_t)(xorshift32() & 0xFF);
    }
}

// Dummy callback that does nothing, just to satisfy the VM
static cnd_error_t fuzz_cb(cnd_vm_ctx* ctx, uint16_t key_id, uint8_t op, void* val) {
    (void)ctx; (void)key_id; (void)op; (void)val;
    return CND_ERR_OK;
}

void fuzz_data(const char* il_path, int iterations) {
    printf("Fuzzing Data Decoding against %s for %d iterations...\n", il_path, iterations);

    // Load IL
    FILE* f = fopen(il_path, "rb");
    if (!f) { printf("Failed to open IL file\n"); return; }
    fseek(f, 0, SEEK_END); long il_size = ftell(f); fseek(f, 0, SEEK_SET);
    uint8_t* il_data = malloc(il_size);
    fread(il_data, 1, il_size, f);
    fclose(f);

    cnd_program prog;
    if (cnd_program_load_il(&prog, il_data, il_size) != CND_ERR_OK) {
        printf("Failed to load IL program\n");
        free(il_data);
        return;
    }

    // Fuzz Loop
    uint8_t buffer[4096]; // Max packet size for fuzzing
    cnd_vm_ctx ctx;
    int error_counts[10] = {0};

    for (int i = 0; i < iterations; i++) {
        // 1. Generate random size (0 to 4096)
        size_t len = xorshift32() % 4097;
        
        // 2. Fill with garbage
        fill_random(buffer, len);

        // 3. Run VM
        // We use a dummy callback because we don't care about the output structure,
        // we only care that the VM doesn't crash reading the input.
        cnd_init(&ctx, CND_MODE_DECODE, &prog, buffer, len, fuzz_cb, NULL);
        
        cnd_error_t err = cnd_execute(&ctx);
        if (err >= 0 && err < 10) error_counts[err]++;
        
        if (i % 10000 == 0) {
            printf("\rIteration %d/%d...", i, iterations);
            fflush(stdout);
        }
    }
    printf("\nDone. No crashes detected.\n");
    printf("Error Distribution:\n");
    for (int i = 0; i <= CND_ERR_CRC_MISMATCH; i++) {
        printf("  %s: %d\n", cnd_error_string((cnd_error_t)i), error_counts[i]);
    }

    free(il_data);
}

void fuzz_il(int iterations) {
    printf("Fuzzing IL Loader for %d iterations...\n", iterations);
    
    uint8_t buffer[1024];
    cnd_program prog;

    for (int i = 0; i < iterations; i++) {
        size_t len = xorshift32() % 1025;
        fill_random(buffer, len);

        // Try to load garbage as a program
        // This checks the header parsing robustness
        cnd_error_t err = cnd_program_load_il(&prog, buffer, len);
        
        // If by some miracle it loads, try to execute it?
        // That might be too aggressive for now, let's just check the loader.
        if (err == CND_ERR_OK) {
            // If it loaded, it thinks it's valid. 
            // We could try to run it, but with garbage bytecode it might hang (infinite loop).
            // For now, just ensuring the loader doesn't crash is good.
        }

        if (i % 10000 == 0) {
            printf("\rIteration %d/%d...", i, iterations);
            fflush(stdout);
        }
    }
    printf("\nDone. No crashes detected.\n");
}

int main(int argc, char** argv) {
    if (argc < 3) {
        printf("Usage:\n");
        printf("  vm_fuzzer data <schema.il> <iterations>\n");
        printf("  vm_fuzzer il <iterations>\n");
        return 1;
    }

    // Seed RNG
    rand_state = (uint32_t)time(NULL);

    if (strcmp(argv[1], "data") == 0) {
        if (argc < 4) { printf("Missing arguments for data fuzzing\n"); return 1; }
        fuzz_data(argv[2], atoi(argv[3]));
    } else if (strcmp(argv[1], "il") == 0) {
        fuzz_il(atoi(argv[2]));
    } else {
        printf("Unknown mode: %s\n", argv[1]);
        return 1;
    }

    return 0;
}
