#include "bench_common.h"

// --- Switch Benchmark ---

struct SwitchBenchData {
    uint8_t type;
    union {
        uint32_t val_a;
        uint64_t val_b;
        uint8_t val_c;
    } u;
};

static cnd_error_t bench_switch_callback(cnd_vm_ctx* ctx, uint16_t key_id, uint8_t type, void* ptr) {
    SwitchBenchData* d = (SwitchBenchData*)ctx->user_ptr;
    
    if (type == OP_CTX_QUERY) { // Switch discriminator query
        *(uint64_t*)ptr = d->type;
        return CND_ERR_OK;
    }
    
    switch (key_id) {
        case 0: // type
            if (ctx->mode == CND_MODE_ENCODE) *(uint8_t*)ptr = d->type;
            else d->type = *(uint8_t*)ptr;
            break;
        case 1: // val_a
            if (ctx->mode == CND_MODE_ENCODE) *(uint32_t*)ptr = d->u.val_a;
            else d->u.val_a = *(uint32_t*)ptr;
            break;
        case 2: // val_b
            if (ctx->mode == CND_MODE_ENCODE) *(uint64_t*)ptr = d->u.val_b;
            else d->u.val_b = *(uint64_t*)ptr;
            break;
        case 3: // val_c
            if (ctx->mode == CND_MODE_ENCODE) *(uint8_t*)ptr = d->u.val_c;
            else d->u.val_c = *(uint8_t*)ptr;
            break;
    }
    return CND_ERR_OK;
}

static void BM_SwitchEncode(benchmark::State& state) {
    const char* schema = R"(
        packet SwitchBench {
            uint8 type;
            switch (type) {
                case 0: uint32 val_a;
                case 1: uint64 val_b;
                default: uint8 val_c;
            }
        }
    )";
    
    std::vector<uint8_t> il_image;
    CompileSchema(schema, il_image);
    
    cnd_program program;
    cnd_program_load_il(&program, il_image.data(), il_image.size());
    
    SwitchBenchData d;
    d.type = 1;
    d.u.val_b = 0x1234567890ABCDEF;
    
    uint8_t buffer[128];
    cnd_vm_ctx ctx;

    for (auto _ : state) {
        memset(buffer, 0, sizeof(buffer));
        cnd_init(&ctx, CND_MODE_ENCODE, &program, buffer, sizeof(buffer), bench_switch_callback, &d);
        cnd_execute(&ctx);
    }
}
BENCHMARK(BM_SwitchEncode);

static void BM_SwitchDecode(benchmark::State& state) {
    const char* schema = R"(
        packet SwitchBench {
            uint8 type;
            switch (type) {
                case 0: uint32 val_a;
                case 1: uint64 val_b;
                default: uint8 val_c;
            }
        }
    )";
    
    std::vector<uint8_t> il_image;
    CompileSchema(schema, il_image);
    
    cnd_program program;
    cnd_program_load_il(&program, il_image.data(), il_image.size());
    
    SwitchBenchData d;
    d.type = 1;
    d.u.val_b = 0x1234567890ABCDEF;
    
    uint8_t buffer[128];
    cnd_vm_ctx ctx;
    
    // Pre-encode
    cnd_init(&ctx, CND_MODE_ENCODE, &program, buffer, sizeof(buffer), bench_switch_callback, &d);
    cnd_execute(&ctx);
    size_t encoded_size = ctx.cursor;

    for (auto _ : state) {
        SwitchBenchData out_d;
        cnd_init(&ctx, CND_MODE_DECODE, &program, buffer, encoded_size, bench_switch_callback, &out_d);
        cnd_execute(&ctx);
    }
}
BENCHMARK(BM_SwitchDecode);
