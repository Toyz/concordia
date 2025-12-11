#include "bench_common.h"

// --- Nested Struct Benchmark ---

struct Point { float x, y, z; };
struct Path { Point start, end; };

struct BenchNestedContext {
    Path path;
    int current_point; // 0 = start, 1 = end
};

static cnd_error_t bench_io_callback_nested(cnd_vm_ctx* ctx, uint16_t key_id, uint8_t type, void* ptr) {
    BenchNestedContext* bc = (BenchNestedContext*)ctx->user_ptr;
    
    if (type == OP_ENTER_STRUCT) {
        // key_id tells us which field in the parent we are entering
        // 0 = start, 1 = end
        bc->current_point = key_id;
        return CND_ERR_OK;
    }
    if (type == OP_EXIT_STRUCT) return CND_ERR_OK;

    Point* p = (bc->current_point == 0) ? &bc->path.start : &bc->path.end;

    switch (key_id) {
        case 0: // x
            if (ctx->mode == CND_MODE_ENCODE) *(float*)ptr = p->x; else p->x = *(float*)ptr;
            break;
        case 1: // y
            if (ctx->mode == CND_MODE_ENCODE) *(float*)ptr = p->y; else p->y = *(float*)ptr;
            break;
        case 2: // z
            if (ctx->mode == CND_MODE_ENCODE) *(float*)ptr = p->z; else p->z = *(float*)ptr;
            break;
    }
    return CND_ERR_OK;
}

static void BM_EncodeNested(benchmark::State& state) {
    std::vector<uint8_t> bytecode;
    CompileSchema(
        "struct Point { float x; float y; float z; }"
        "packet Path { Point start; Point end; }", 
        bytecode);
    
    cnd_program program;
    cnd_program_load_il(&program, bytecode.data(), bytecode.size());
    
    BenchNestedContext bc;
    bc.path.start = {1.0f, 2.0f, 3.0f};
    bc.path.end = {4.0f, 5.0f, 6.0f};
    
    uint8_t buffer[128];
    cnd_vm_ctx ctx;

    for (auto _ : state) {
        memset(buffer, 0, sizeof(buffer));
        cnd_init(&ctx, CND_MODE_ENCODE, &program, buffer, sizeof(buffer), bench_io_callback_nested, &bc);
        cnd_execute(&ctx);
    }
}
BENCHMARK(BM_EncodeNested);

static void BM_DecodeNested(benchmark::State& state) {
    std::vector<uint8_t> bytecode;
    CompileSchema(
        "struct Point { float x; float y; float z; }"
        "packet Path { Point start; Point end; }", 
        bytecode);
    
    cnd_program program;
    cnd_program_load_il(&program, bytecode.data(), bytecode.size());
    
    BenchNestedContext bc;
    bc.path.start = {1.0f, 2.0f, 3.0f};
    bc.path.end = {4.0f, 5.0f, 6.0f};
    
    uint8_t buffer[128];
    cnd_vm_ctx ctx;
    
    // Pre-encode
    cnd_init(&ctx, CND_MODE_ENCODE, &program, buffer, sizeof(buffer), bench_io_callback_nested, &bc);
    cnd_execute(&ctx);
    size_t encoded_size = ctx.cursor;

    for (auto _ : state) {
        BenchNestedContext out_bc;
        cnd_init(&ctx, CND_MODE_DECODE, &program, buffer, encoded_size, bench_io_callback_nested, &out_bc);
        cnd_execute(&ctx);
    }
}
BENCHMARK(BM_DecodeNested);

// --- Array of Structs Benchmark ---

struct Item { uint32_t id; uint16_t val; };
struct ItemList { Item items[100]; };

struct BenchArrayStructContext {
    ItemList list;
    int current_idx;
};

static cnd_error_t bench_io_callback_large_array(cnd_vm_ctx* ctx, uint16_t key_id, uint8_t type, void* ptr) {
    (void)key_id;
    BenchContext* bc = (BenchContext*)ctx->user_ptr;
    
    if (type == OP_ARR_FIXED) {
        bc->array_idx = 0;
        return CND_ERR_OK;
    }
    if (type == OP_ARR_END || type == OP_ENTER_STRUCT || type == OP_EXIT_STRUCT) return CND_ERR_OK;

    if (type == OP_RAW_BYTES) {
        // Bulk copy optimization
        // We know it's the data array (key 0 probably, or whatever key the element has)
        // For this benchmark, we just copy 1024 bytes (or however many remaining)
        // But wait, OP_RAW_BYTES doesn't tell us the size in the callback params directly?
        // The VM passes a pointer to the buffer.
        // We need to know how much to copy.
        // The callback signature is (ctx, key, type, ptr).
        // It doesn't pass size.
        // The user must know the size from the schema/key?
        // Or we assume the buffer at ptr is valid for the size implied by the schema.
        
        // For this benchmark, we know it's 1024 bytes.
        if (ctx->mode == CND_MODE_ENCODE) {
            memcpy(ptr, bc->data.data, 1024);
        } else {
            memcpy(bc->data.data, ptr, 1024);
        }
        return CND_ERR_OK;
    }

    if (type == OP_IO_U8) {
        if (bc->array_idx < 1024) {
            if (ctx->mode == CND_MODE_ENCODE) *(uint8_t*)ptr = bc->data.data[bc->array_idx % 16];
            else bc->data.data[bc->array_idx % 16] = *(uint8_t*)ptr;
            bc->array_idx++;
        }
    }
    return CND_ERR_OK;
}

static cnd_error_t bench_io_callback_array_struct(cnd_vm_ctx* ctx, uint16_t key_id, uint8_t type, void* ptr) {
    BenchArrayStructContext* bc = (BenchArrayStructContext*)ctx->user_ptr;
    
    if (type == OP_ARR_FIXED) {
        bc->current_idx = 0;
        if (ctx->mode == CND_MODE_ENCODE) *(uint16_t*)ptr = 100;
        return CND_ERR_OK;
    }
    if (type == OP_ENTER_STRUCT) return CND_ERR_OK;
    if (type == OP_EXIT_STRUCT) {
        bc->current_idx++;
        return CND_ERR_OK;
    }
    if (type == OP_ARR_END) return CND_ERR_OK;

    // Inside Item struct
    // Key IDs: items=0, items.id=1, items.val=2
    if (bc->current_idx >= 100) return CND_ERR_OOB;
    Item* item = &bc->list.items[bc->current_idx];

    switch (key_id) {
        case 1: // items.id
            if (ctx->mode == CND_MODE_ENCODE) *(uint32_t*)ptr = item->id; else item->id = *(uint32_t*)ptr;
            break;
        case 2: // items.val
            if (ctx->mode == CND_MODE_ENCODE) *(uint16_t*)ptr = item->val; else item->val = *(uint16_t*)ptr;
            break;
    }
    return CND_ERR_OK;
}

static void BM_EncodeArrayStruct(benchmark::State& state) {
    std::vector<uint8_t> bytecode;
    CompileSchema(
        "struct Item { uint32 id; uint16 val; }"
        "packet List { Item items[100]; }", 
        bytecode);
    
    cnd_program program;
    cnd_program_load_il(&program, bytecode.data(), bytecode.size());
    
    BenchArrayStructContext bc;
    for(int i=0; i<100; i++) { bc.list.items[i] = {(uint32_t)i, (uint16_t)(i*2)}; }
    
    uint8_t buffer[1024]; // Needs to be larger
    cnd_vm_ctx ctx;

    for (auto _ : state) {
        memset(buffer, 0, sizeof(buffer));
        cnd_init(&ctx, CND_MODE_ENCODE, &program, buffer, sizeof(buffer), bench_io_callback_array_struct, &bc);
        cnd_execute(&ctx);
    }
}
BENCHMARK(BM_EncodeArrayStruct);

static void BM_DecodeArrayStruct(benchmark::State& state) {
    std::vector<uint8_t> bytecode;
    CompileSchema(
        "struct Item { uint32 id; uint16 val; }"
        "packet List { Item items[100]; }", 
        bytecode);
    
    cnd_program program;
    cnd_program_load_il(&program, bytecode.data(), bytecode.size());
    
    BenchArrayStructContext bc;
    for(int i=0; i<100; i++) { bc.list.items[i] = {(uint32_t)i, (uint16_t)(i*2)}; }
    
    uint8_t buffer[1024]; 
    cnd_vm_ctx ctx;
    
    // Pre-encode
    cnd_init(&ctx, CND_MODE_ENCODE, &program, buffer, sizeof(buffer), bench_io_callback_array_struct, &bc);
    cnd_execute(&ctx);
    size_t encoded_size = ctx.cursor;

    for (auto _ : state) {
        BenchArrayStructContext out_bc;
        cnd_init(&ctx, CND_MODE_DECODE, &program, buffer, encoded_size, bench_io_callback_array_struct, &out_bc);
        cnd_execute(&ctx);
    }
}
BENCHMARK(BM_DecodeArrayStruct);

static void BM_EncodeBigEndian(benchmark::State& state) {
    std::vector<uint8_t> bytecode;
    CompileSchema("packet P { @big_endian uint32 val; }", bytecode);
    
    cnd_program program;
    cnd_program_load_il(&program, bytecode.data(), bytecode.size());
    
    BenchData d = { 0x12345678, 0, {0} };
    
    uint8_t buffer[128];
    cnd_vm_ctx ctx;

    for (auto _ : state) {
        memset(buffer, 0, sizeof(buffer));
        cnd_init(&ctx, CND_MODE_ENCODE, &program, buffer, sizeof(buffer), bench_io_callback, &d);
        cnd_execute(&ctx);
    }
}
BENCHMARK(BM_EncodeBigEndian);

static void BM_EncodeLargeArray(benchmark::State& state) {
    std::vector<uint8_t> bytecode;
    CompileSchema("packet P { uint8 data[1024]; }", bytecode);
    
    cnd_program program;
    cnd_program_load_il(&program, bytecode.data(), bytecode.size());
    
    BenchContext bc;
    memset(bc.data.data, 0xAA, 16); // Just dummy
    
    uint8_t buffer[2048];
    cnd_vm_ctx ctx;

    for (auto _ : state) {
        memset(buffer, 0, sizeof(buffer));
        cnd_init(&ctx, CND_MODE_ENCODE, &program, buffer, sizeof(buffer), bench_io_callback_large_array, &bc);
        cnd_execute(&ctx);
    }
}
BENCHMARK(BM_EncodeLargeArray);

static void BM_EncodeSimple(benchmark::State& state) {
    std::vector<uint8_t> il_image;
    CompileSchema("packet P { uint32 id; float val; uint8 data[16]; }", il_image);
    
    cnd_program program;
    cnd_program_load_il(&program, il_image.data(), il_image.size());
    
    BenchContext bc;
    bc.data.id = 0x12345678;
    bc.data.val = 3.14159f;
    memset(bc.data.data, 0xAA, 16);
    
    uint8_t buffer[128];
    cnd_vm_ctx ctx;

    for (auto _ : state) {
        memset(buffer, 0, sizeof(buffer));
        cnd_init(&ctx, CND_MODE_ENCODE, &program, buffer, sizeof(buffer), bench_io_callback_complex, &bc);
        cnd_execute(&ctx);
    }
}
BENCHMARK(BM_EncodeSimple);

static void BM_DecodeSimple(benchmark::State& state) {
    std::vector<uint8_t> il_image;
    CompileSchema("packet P { uint32 id; float val; uint8 data[16]; }", il_image);
    
    cnd_program program;
    cnd_program_load_il(&program, il_image.data(), il_image.size());
    
    BenchContext bc;
    bc.data.id = 0x12345678;
    bc.data.val = 3.14159f;
    memset(bc.data.data, 0xAA, 16);
    
    uint8_t buffer[128];
    cnd_vm_ctx ctx;
    
    // Pre-encode
    cnd_init(&ctx, CND_MODE_ENCODE, &program, buffer, sizeof(buffer), bench_io_callback_complex, &bc);
    cnd_execute(&ctx);
    size_t encoded_size = ctx.cursor;

    for (auto _ : state) {
        BenchContext out_bc;
        cnd_init(&ctx, CND_MODE_DECODE, &program, buffer, encoded_size, bench_io_callback_complex, &out_bc);
        cnd_execute(&ctx);
    }
}
BENCHMARK(BM_DecodeSimple);
