#include "bench_common.h"

// --- If Benchmark ---

struct IfBenchData {
    bool condition;
    uint32_t value;
};

static cnd_error_t bench_if_callback(cnd_vm_ctx* ctx, uint16_t key_id, uint8_t type, void* ptr) {
    IfBenchData* d = (IfBenchData*)ctx->user_ptr;
    
    if (type == OP_LOAD_CTX) {
        // Used for condition check
        if (key_id == 0) {
            *(uint64_t*)ptr = d->condition ? 1 : 0;
            return CND_ERR_OK;
        }
    }

    switch(key_id) {
        case 0: // condition
            if (ctx->mode == CND_MODE_ENCODE) *(uint8_t*)ptr = d->condition ? 1 : 0;
            else d->condition = (*(uint8_t*)ptr) != 0;
            break;
        case 1: // value
            if (ctx->mode == CND_MODE_ENCODE) *(uint32_t*)ptr = d->value;
            else d->value = *(uint32_t*)ptr;
            break;
    }
    return CND_ERR_OK;
}

static void BM_IfEncode(benchmark::State& state) {
    std::vector<uint8_t> il_image;
    CompileSchema(
        "packet P {"
        "  bool condition;"
        "  if (condition) {"
        "    uint32 value;"
        "  }"
        "}", 
        il_image
    );
    
    cnd_program program;
    cnd_program_load_il(&program, il_image.data(), il_image.size());
    
    IfBenchData d = { true, 0x12345678 };
    
    uint8_t buffer[128];
    cnd_vm_ctx ctx;

    for (auto _ : state) {
        memset(buffer, 0, sizeof(buffer));
        cnd_init(&ctx, CND_MODE_ENCODE, &program, buffer, sizeof(buffer), bench_if_callback, &d);
        cnd_execute(&ctx);
    }
}
BENCHMARK(BM_IfEncode);

static void BM_IfDecode(benchmark::State& state) {
    std::vector<uint8_t> il_image;
    CompileSchema(
        "packet P {"
        "  bool condition;"
        "  if (condition) {"
        "    uint32 value;"
        "  }"
        "}", 
        il_image
    );
    
    cnd_program program;
    cnd_program_load_il(&program, il_image.data(), il_image.size());
    
    IfBenchData d = { true, 0x12345678 };
    
    uint8_t buffer[128];
    cnd_vm_ctx ctx;
    
    // Pre-encode
    cnd_init(&ctx, CND_MODE_ENCODE, &program, buffer, sizeof(buffer), bench_if_callback, &d);
    cnd_execute(&ctx);
    size_t encoded_size = ctx.cursor;

    for (auto _ : state) {
        IfBenchData out_d;
        cnd_init(&ctx, CND_MODE_DECODE, &program, buffer, encoded_size, bench_if_callback, &out_d);
        cnd_execute(&ctx);
    }
}
BENCHMARK(BM_IfDecode);

// --- If/Else Benchmarks ---

struct IfElseData {
    bool condition;
    uint32_t val_true;
    uint32_t val_false;
};

static cnd_error_t bench_ifelse_callback(cnd_vm_ctx* ctx, uint16_t key_id, uint8_t type, void* ptr) {
    IfElseData* d = (IfElseData*)ctx->user_ptr;
    
    // Key mapping:
    // 0: condition
    // 1: val_true
    // 2: val_false
    
    if (type == OP_LOAD_CTX) {
        // The VM is asking for the value of 'condition' to decide the jump
        if (key_id == 0) {
            *(uint64_t*)ptr = d->condition ? 1 : 0;
            return CND_ERR_OK;
        }
    }

    switch (key_id) {
        case 0: // condition
            if (ctx->mode == CND_MODE_ENCODE) *(uint8_t*)ptr = d->condition ? 1 : 0;
            else d->condition = (*(uint8_t*)ptr) != 0;
            break;
        case 1: // val_true
            if (ctx->mode == CND_MODE_ENCODE) *(uint32_t*)ptr = d->val_true;
            else d->val_true = *(uint32_t*)ptr;
            break;
        case 2: // val_false
            if (ctx->mode == CND_MODE_ENCODE) *(uint32_t*)ptr = d->val_false;
            else d->val_false = *(uint32_t*)ptr;
            break;
    }
    return CND_ERR_OK;
}

static void BM_EncodeIfElse_True(benchmark::State& state) {
    std::vector<uint8_t> bytecode;
    CompileSchema(
        "packet IfElseBench { bool condition; if (condition) { uint32 val_true; } else { uint32 val_false; } }", 
        bytecode);
    
    cnd_program program;
    cnd_program_load_il(&program, bytecode.data(), bytecode.size());
    
    IfElseData data;
    data.condition = true;
    data.val_true = 12345;
    data.val_false = 67890;
    
    uint8_t buffer[128];
    cnd_vm_ctx ctx;

    for (auto _ : state) {
        memset(buffer, 0, sizeof(buffer));
        cnd_init(&ctx, CND_MODE_ENCODE, &program, buffer, sizeof(buffer), bench_ifelse_callback, &data);
        cnd_execute(&ctx);
    }
}
BENCHMARK(BM_EncodeIfElse_True);

static void BM_EncodeIfElse_False(benchmark::State& state) {
    std::vector<uint8_t> bytecode;
    CompileSchema(
        "packet IfElseBench { bool condition; if (condition) { uint32 val_true; } else { uint32 val_false; } }", 
        bytecode);
    
    cnd_program program;
    cnd_program_load_il(&program, bytecode.data(), bytecode.size());
    
    IfElseData data;
    data.condition = false;
    data.val_true = 12345;
    data.val_false = 67890;
    
    uint8_t buffer[128];
    cnd_vm_ctx ctx;

    for (auto _ : state) {
        memset(buffer, 0, sizeof(buffer));
        cnd_init(&ctx, CND_MODE_ENCODE, &program, buffer, sizeof(buffer), bench_ifelse_callback, &data);
        cnd_execute(&ctx);
    }
}
BENCHMARK(BM_EncodeIfElse_False);

static void BM_EncodeNestedIfElse(benchmark::State& state) {
    std::vector<uint8_t> bytecode;
    CompileSchema(
        "packet NestedIfElse { bool a; bool b; if (a) { if (b) { uint32 val_aa; } else { uint32 val_ab; } } else { uint32 val_b; } }", 
        bytecode);
    
    cnd_program program;
    cnd_program_load_il(&program, bytecode.data(), bytecode.size());
    
    struct NestedData {
        bool a;
        bool b;
        uint32_t val_aa;
        uint32_t val_ab;
        uint32_t val_b;
    } data;
    
    data.a = true;
    data.b = false;
    data.val_aa = 1;
    data.val_ab = 2;
    data.val_b = 3;
    
    auto callback = [](cnd_vm_ctx* ctx, uint16_t key_id, uint8_t type, void* ptr) -> cnd_error_t {
        NestedData* d = (NestedData*)ctx->user_ptr;
        
        if (type == OP_LOAD_CTX) {
            if (key_id == 0) *(uint64_t*)ptr = d->a ? 1 : 0;
            else if (key_id == 1) *(uint64_t*)ptr = d->b ? 1 : 0;
            return CND_ERR_OK;
        }

        switch (key_id) {
            case 0: // a
                if (ctx->mode == CND_MODE_ENCODE) *(uint8_t*)ptr = d->a ? 1 : 0;
                else d->a = (*(uint8_t*)ptr) != 0;
                break;
            case 1: // b
                if (ctx->mode == CND_MODE_ENCODE) *(uint8_t*)ptr = d->b ? 1 : 0;
                else d->b = (*(uint8_t*)ptr) != 0;
                break;
            case 2: // val_aa
                if (ctx->mode == CND_MODE_ENCODE) *(uint32_t*)ptr = d->val_aa;
                else d->val_aa = *(uint32_t*)ptr;
                break;
            case 3: // val_ab
                if (ctx->mode == CND_MODE_ENCODE) *(uint32_t*)ptr = d->val_ab;
                else d->val_ab = *(uint32_t*)ptr;
                break;
            case 4: // val_b
                if (ctx->mode == CND_MODE_ENCODE) *(uint32_t*)ptr = d->val_b;
                else d->val_b = *(uint32_t*)ptr;
                break;
        }
        return CND_ERR_OK;
    };
    
    uint8_t buffer[128];
    cnd_vm_ctx ctx;

    for (auto _ : state) {
        memset(buffer, 0, sizeof(buffer));
        cnd_init(&ctx, CND_MODE_ENCODE, &program, buffer, sizeof(buffer), callback, &data);
        cnd_execute(&ctx);
    }
}
BENCHMARK(BM_EncodeNestedIfElse);

static void BM_DecodeIfElse_True(benchmark::State& state) {
    std::vector<uint8_t> bytecode;
    CompileSchema(
        "packet IfElseBench { bool condition; if (condition) { uint32 val_true; } else { uint32 val_false; } }", 
        bytecode);
    
    cnd_program program;
    cnd_program_load_il(&program, bytecode.data(), bytecode.size());
    
    IfElseData data;
    data.condition = true;
    data.val_true = 12345;
    data.val_false = 67890;
    
    uint8_t buffer[128];
    cnd_vm_ctx ctx;
    
    // Pre-encode
    cnd_init(&ctx, CND_MODE_ENCODE, &program, buffer, sizeof(buffer), bench_ifelse_callback, &data);
    cnd_execute(&ctx);
    size_t encoded_size = ctx.cursor;

    for (auto _ : state) {
        IfElseData out_d;
        cnd_init(&ctx, CND_MODE_DECODE, &program, buffer, encoded_size, bench_ifelse_callback, &out_d);
        cnd_execute(&ctx);
    }
}
BENCHMARK(BM_DecodeIfElse_True);

static void BM_DecodeIfElse_False(benchmark::State& state) {
    std::vector<uint8_t> bytecode;
    CompileSchema(
        "packet IfElseBench { bool condition; if (condition) { uint32 val_true; } else { uint32 val_false; } }", 
        bytecode);
    
    cnd_program program;
    cnd_program_load_il(&program, bytecode.data(), bytecode.size());
    
    IfElseData data;
    data.condition = false;
    data.val_true = 12345;
    data.val_false = 67890;
    
    uint8_t buffer[128];
    cnd_vm_ctx ctx;
    
    // Pre-encode
    cnd_init(&ctx, CND_MODE_ENCODE, &program, buffer, sizeof(buffer), bench_ifelse_callback, &data);
    cnd_execute(&ctx);
    size_t encoded_size = ctx.cursor;

    for (auto _ : state) {
        IfElseData out_d;
        cnd_init(&ctx, CND_MODE_DECODE, &program, buffer, encoded_size, bench_ifelse_callback, &out_d);
        cnd_execute(&ctx);
    }
}
BENCHMARK(BM_DecodeIfElse_False);

static void BM_DecodeNestedIfElse(benchmark::State& state) {
    std::vector<uint8_t> bytecode;
    CompileSchema(
        "packet NestedIfElse { bool a; bool b; if (a) { if (b) { uint32 val_aa; } else { uint32 val_ab; } } else { uint32 val_b; } }", 
        bytecode);
    
    cnd_program program;
    cnd_program_load_il(&program, bytecode.data(), bytecode.size());
    
    struct NestedData {
        bool a;
        bool b;
        uint32_t val_aa;
        uint32_t val_ab;
        uint32_t val_b;
    } data;
    
    data.a = true;
    data.b = false;
    data.val_aa = 1;
    data.val_ab = 2;
    data.val_b = 3;
    
    auto callback = [](cnd_vm_ctx* ctx, uint16_t key_id, uint8_t type, void* ptr) -> cnd_error_t {
        NestedData* d = (NestedData*)ctx->user_ptr;
        
        if (type == OP_LOAD_CTX) {
            if (key_id == 0) *(uint64_t*)ptr = d->a ? 1 : 0;
            else if (key_id == 1) *(uint64_t*)ptr = d->b ? 1 : 0;
            return CND_ERR_OK;
        }

        switch (key_id) {
            case 0: // a
                if (ctx->mode == CND_MODE_ENCODE) *(uint8_t*)ptr = d->a ? 1 : 0;
                else d->a = (*(uint8_t*)ptr) != 0;
                break;
            case 1: // b
                if (ctx->mode == CND_MODE_ENCODE) *(uint8_t*)ptr = d->b ? 1 : 0;
                else d->b = (*(uint8_t*)ptr) != 0;
                break;
            case 2: // val_aa
                if (ctx->mode == CND_MODE_ENCODE) *(uint32_t*)ptr = d->val_aa;
                else d->val_aa = *(uint32_t*)ptr;
                break;
            case 3: // val_ab
                if (ctx->mode == CND_MODE_ENCODE) *(uint32_t*)ptr = d->val_ab;
                else d->val_ab = *(uint32_t*)ptr;
                break;
            case 4: // val_b
                if (ctx->mode == CND_MODE_ENCODE) *(uint32_t*)ptr = d->val_b;
                else d->val_b = *(uint32_t*)ptr;
                break;
        }
        return CND_ERR_OK;
    };
    
    uint8_t buffer[128];
    cnd_vm_ctx ctx;
    
    // Pre-encode
    cnd_init(&ctx, CND_MODE_ENCODE, &program, buffer, sizeof(buffer), callback, &data);
    cnd_execute(&ctx);
    size_t encoded_size = ctx.cursor;

    for (auto _ : state) {
        NestedData out_d;
        cnd_init(&ctx, CND_MODE_DECODE, &program, buffer, encoded_size, callback, &out_d);
        cnd_execute(&ctx);
    }
}
BENCHMARK(BM_DecodeNestedIfElse);
