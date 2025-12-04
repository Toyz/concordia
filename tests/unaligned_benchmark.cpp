#include <benchmark/benchmark.h>
#include "concordia.h"
#include <vector>
#include <cstring>

// Mock callback
static cnd_error_t bench_io_callback(cnd_vm_ctx* ctx, uint16_t key, uint8_t op, void* val) {
    (void)ctx; (void)key; (void)op; (void)val;
    return CND_ERR_OK;
}

static void BM_UnalignedRead(benchmark::State& state) {
    uint8_t il[] = {
        OP_ENTER_BIT_MODE,
        OP_SET_ENDIAN_BE,
        OP_IO_BIT_U, 0x00, 0x00, 3,
        OP_IO_BIT_U, 0x01, 0x00, 5,
        OP_IO_BIT_U, 0x02, 0x00, 10,
        OP_IO_BIT_U, 0x03, 0x00, 6,
        OP_EXIT_BIT_MODE
    };
    
    cnd_program program;
    cnd_program_load(&program, il, sizeof(il));
    
    uint8_t buffer[1024];
    memset(buffer, 0xAA, sizeof(buffer)); // Dummy data
    
    cnd_vm_ctx ctx;
    cnd_init(&ctx, CND_MODE_DECODE, &program, buffer, sizeof(buffer), bench_io_callback, NULL);
    
    for (auto _ : state) {
        ctx.ip = 0;
        ctx.cursor = 0;
        ctx.bit_offset = 0;
        cnd_execute(&ctx);
    }
}
BENCHMARK(BM_UnalignedRead);

static void BM_AlignedRead(benchmark::State& state) {
    uint8_t il[] = {
        OP_IO_U8, 0x00, 0x00,
        OP_IO_U8, 0x01, 0x00,
        OP_IO_U16, 0x02, 0x00,
        OP_IO_U8, 0x03, 0x00
    };
    
    cnd_program program;
    cnd_program_load(&program, il, sizeof(il));
    
    uint8_t buffer[1024];
    memset(buffer, 0xAA, sizeof(buffer));
    
    cnd_vm_ctx ctx;
    cnd_init(&ctx, CND_MODE_DECODE, &program, buffer, sizeof(buffer), bench_io_callback, NULL);
    
    for (auto _ : state) {
        ctx.ip = 0;
        ctx.cursor = 0;
        ctx.bit_offset = 0;
        cnd_execute(&ctx);
    }
}
BENCHMARK(BM_AlignedRead);

BENCHMARK_MAIN();
