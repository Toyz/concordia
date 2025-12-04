#include "bench_common.h"
#include <math.h>

// --- Expr Benchmark ---

struct ExprData {
    uint32_t x;
    float res;
};

static cnd_error_t bench_io_callback_expr(cnd_vm_ctx* ctx, uint16_t key_id, uint8_t type, void* ptr) {
    (void)type;
    ExprData* d = (ExprData*)ctx->user_ptr;
    switch (key_id) {
        case 0: // x
            if (ctx->mode == CND_MODE_ENCODE) *(uint32_t*)ptr = d->x; 
            else d->x = *(uint32_t*)ptr;
            break;
        case 1: // res
            // For expr, we don't provide value in Encode (it's calculated)
            break;
    }
    return CND_ERR_OK;
}

static void BM_EncodeExprSimple(benchmark::State& state) {
    std::vector<uint8_t> bytecode;
    CompileSchema(
        "packet P { uint32 x; @expr(x * 2 + 5) uint32 res; }", 
        bytecode);
    
    cnd_program program;
    cnd_program_load_il(&program, bytecode.data(), bytecode.size());
    
    ExprData d = { 10, 0 };
    uint8_t buffer[128];
    cnd_vm_ctx ctx;

    for (auto _ : state) {
        memset(buffer, 0, sizeof(buffer));
        cnd_init(&ctx, CND_MODE_ENCODE, &program, buffer, sizeof(buffer), bench_io_callback_expr, &d);
        cnd_execute(&ctx);
    }
}
BENCHMARK(BM_EncodeExprSimple);

static void BM_EncodeExprMath(benchmark::State& state) {
    std::vector<uint8_t> bytecode;
    CompileSchema(
        "packet P { uint32 x; @expr(sin(float(x)) * cos(float(x))) float res; }", 
        bytecode);
    
    cnd_program program;
    cnd_program_load_il(&program, bytecode.data(), bytecode.size());
    
    ExprData d = { 10, 0 };
    uint8_t buffer[128];
    cnd_vm_ctx ctx;

    for (auto _ : state) {
        memset(buffer, 0, sizeof(buffer));
        cnd_init(&ctx, CND_MODE_ENCODE, &program, buffer, sizeof(buffer), bench_io_callback_expr, &d);
        cnd_execute(&ctx);
    }
}
BENCHMARK(BM_EncodeExprMath);

// --- Poly Benchmark ---

struct PolyData {
    double val;
};

static cnd_error_t bench_io_callback_poly(cnd_vm_ctx* ctx, uint16_t key_id, uint8_t type, void* ptr) {
    (void)type;
    PolyData* d = (PolyData*)ctx->user_ptr;
    if (key_id == 0) {
        if (ctx->mode == CND_MODE_ENCODE) *(double*)ptr = d->val;
        else d->val = *(double*)ptr;
    }
    return CND_ERR_OK;
}

static void BM_EncodePoly(benchmark::State& state) {
    std::vector<uint8_t> bytecode;
    CompileSchema(
        "packet P { @poly(0.5, 2.0, 1.5) uint8 val; }", 
        bytecode);
    
    cnd_program program;
    cnd_program_load_il(&program, bytecode.data(), bytecode.size());
    
    PolyData d = { 100.0 }; // Engineering value
    uint8_t buffer[128];
    cnd_vm_ctx ctx;

    for (auto _ : state) {
        memset(buffer, 0, sizeof(buffer));
        cnd_init(&ctx, CND_MODE_ENCODE, &program, buffer, sizeof(buffer), bench_io_callback_poly, &d);
        cnd_execute(&ctx);
    }
}
BENCHMARK(BM_EncodePoly);

static void BM_DecodePoly(benchmark::State& state) {
    std::vector<uint8_t> bytecode;
    CompileSchema(
        "packet P { @poly(0.5, 2.0, 1.5) uint8 val; }", 
        bytecode);
    
    cnd_program program;
    cnd_program_load_il(&program, bytecode.data(), bytecode.size());
    
    PolyData d = { 100.0 };
    uint8_t buffer[128];
    cnd_vm_ctx ctx;
    
    cnd_init(&ctx, CND_MODE_ENCODE, &program, buffer, sizeof(buffer), bench_io_callback_poly, &d);
    cnd_execute(&ctx);
    size_t encoded_size = ctx.cursor;

    for (auto _ : state) {
        PolyData out_d;
        cnd_init(&ctx, CND_MODE_DECODE, &program, buffer, encoded_size, bench_io_callback_poly, &out_d);
        cnd_execute(&ctx);
    }
}
BENCHMARK(BM_DecodePoly);

// --- Spline Benchmark ---

static void BM_EncodeSpline(benchmark::State& state) {
    std::vector<uint8_t> bytecode;
    CompileSchema(
        "packet P { @spline(0.0, 0.0, 10.0, 100.0, 20.0, 400.0, 30.0, 900.0) uint8 val; }", 
        bytecode);
    
    cnd_program program;
    cnd_program_load_il(&program, bytecode.data(), bytecode.size());
    
    PolyData d = { 250.0 }; // Engineering value
    uint8_t buffer[128];
    cnd_vm_ctx ctx;

    for (auto _ : state) {
        memset(buffer, 0, sizeof(buffer));
        cnd_init(&ctx, CND_MODE_ENCODE, &program, buffer, sizeof(buffer), bench_io_callback_poly, &d);
        cnd_execute(&ctx);
    }
}
BENCHMARK(BM_EncodeSpline);

static void BM_DecodeSpline(benchmark::State& state) {
    std::vector<uint8_t> bytecode;
    CompileSchema(
        "packet P { @spline(0.0, 0.0, 10.0, 100.0, 20.0, 400.0, 30.0, 900.0) uint8 val; }", 
        bytecode);
    
    cnd_program program;
    cnd_program_load_il(&program, bytecode.data(), bytecode.size());
    
    PolyData d = { 250.0 };
    uint8_t buffer[128];
    cnd_vm_ctx ctx;
    
    cnd_init(&ctx, CND_MODE_ENCODE, &program, buffer, sizeof(buffer), bench_io_callback_poly, &d);
    cnd_execute(&ctx);
    size_t encoded_size = ctx.cursor;

    for (auto _ : state) {
        PolyData out_d;
        cnd_init(&ctx, CND_MODE_DECODE, &program, buffer, encoded_size, bench_io_callback_poly, &out_d);
        cnd_execute(&ctx);
    }
}
BENCHMARK(BM_DecodeSpline);
