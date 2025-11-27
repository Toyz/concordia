#include <benchmark/benchmark.h>
#include "../include/concordia.h"
#include "../include/compiler.h"
#include <cstring>
#include <vector>

// Forward declaration
static void CompileSchema(const char* schema, std::vector<uint8_t>& bytecode);

// --- Mock Data & Callback ---

struct BenchData {
    uint32_t id;
    float val;
    uint8_t data[16];
};

static cnd_error_t bench_io_callback(cnd_vm_ctx* ctx, uint16_t key_id, uint8_t type, void* ptr) {
    BenchData* d = (BenchData*)ctx->user_ptr;
    // Simple mapping based on order in schema
    // 0: id, 1: val, 2: data (array start), 3: data (item)
    
    if (type == OP_ARR_FIXED || type == OP_ARR_END || type == OP_ENTER_STRUCT || type == OP_EXIT_STRUCT) return CND_ERR_OK;

    switch (key_id) {
        case 0: // id
            if (ctx->mode == CND_MODE_ENCODE) *(uint32_t*)ptr = d->id;
            else d->id = *(uint32_t*)ptr;
            break;
        case 1: // val
            if (ctx->mode == CND_MODE_ENCODE) *(float*)ptr = d->val;
            else d->val = *(float*)ptr;
            break;
        case 2: // data array
            if (type == OP_IO_U8) {
                // We are inside the array loop, but we don't track index in this simple callback easily without context state
                // For benchmarking, let's just read/write to the first byte or a dummy
                // To do it correctly we need a cursor in user_ptr or similar.
                // Let's simplify: Just do scalar fields for basic VM overhead benchmark first.
            }
            break;
    }
    return CND_ERR_OK;
}

// Better callback for array support
struct BenchContext {
    BenchData data;
    int array_idx;
};

static cnd_error_t bench_io_callback_complex(cnd_vm_ctx* ctx, uint16_t key_id, uint8_t type, void* ptr) {
    BenchContext* bc = (BenchContext*)ctx->user_ptr;
    
    if (type == OP_ARR_FIXED) {
        bc->array_idx = 0;
        if (ctx->mode == CND_MODE_ENCODE) *(uint16_t*)ptr = 16; // Count
        return CND_ERR_OK;
    }
    if (type == OP_ARR_END || type == OP_ENTER_STRUCT || type == OP_EXIT_STRUCT) return CND_ERR_OK;

    switch (key_id) {
        case 0: // id
            if (ctx->mode == CND_MODE_ENCODE) *(uint32_t*)ptr = bc->data.id;
            else bc->data.id = *(uint32_t*)ptr;
            break;
        case 1: // val
            if (ctx->mode == CND_MODE_ENCODE) *(float*)ptr = bc->data.val;
            else bc->data.val = *(float*)ptr;
            break;
        case 2: // data array item
            if (type == OP_IO_U8) {
                if (bc->array_idx < 16) {
                    if (ctx->mode == CND_MODE_ENCODE) *(uint8_t*)ptr = bc->data.data[bc->array_idx];
                    else bc->data.data[bc->array_idx] = *(uint8_t*)ptr;
                    bc->array_idx++;
                }
            }
            break;
    }
    return CND_ERR_OK;
}

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
    cnd_program_load(&program, bytecode.data(), bytecode.size());
    
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
    cnd_program_load(&program, bytecode.data(), bytecode.size());
    
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
    if (bc->current_idx >= 100) return CND_ERR_OOB;
    Item* item = &bc->list.items[bc->current_idx];

    switch (key_id) {
        case 0: // id
            if (ctx->mode == CND_MODE_ENCODE) *(uint32_t*)ptr = item->id; else item->id = *(uint32_t*)ptr;
            break;
        case 1: // val
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
    cnd_program_load(&program, bytecode.data(), bytecode.size());
    
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
    cnd_program_load(&program, bytecode.data(), bytecode.size());
    
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

// --- Bitfield Benchmark ---

struct Flags {
    uint32_t a; // 5 bits
    uint32_t b; // 12 bits
    uint32_t c; // 3 bits
    uint32_t d; // 12 bits
};

static cnd_error_t bench_io_callback_bitfield(cnd_vm_ctx* ctx, uint16_t key_id, uint8_t type, void* ptr) {
    Flags* f = (Flags*)ctx->user_ptr;
    switch (key_id) {
        case 0: if (ctx->mode == CND_MODE_ENCODE) *(uint64_t*)ptr = f->a; else f->a = (uint32_t)*(uint64_t*)ptr; break;
        case 1: if (ctx->mode == CND_MODE_ENCODE) *(uint64_t*)ptr = f->b; else f->b = (uint32_t)*(uint64_t*)ptr; break;
        case 2: if (ctx->mode == CND_MODE_ENCODE) *(uint64_t*)ptr = f->c; else f->c = (uint32_t)*(uint64_t*)ptr; break;
        case 3: if (ctx->mode == CND_MODE_ENCODE) *(uint64_t*)ptr = f->d; else f->d = (uint32_t)*(uint64_t*)ptr; break;
    }
    return CND_ERR_OK;
}

static void BM_EncodeBitfields(benchmark::State& state) {
    std::vector<uint8_t> bytecode;
    CompileSchema(
        "packet Flags { uint32 a:5; uint32 b:12; uint32 c:3; uint32 d:12; }", 
        bytecode);
    
    cnd_program program;
    cnd_program_load(&program, bytecode.data(), bytecode.size());
    
    Flags f = { 0x1F, 0xABC, 0x7, 0xFFF };
    
    uint8_t buffer[128];
    cnd_vm_ctx ctx;

    for (auto _ : state) {
        memset(buffer, 0, sizeof(buffer));
        cnd_init(&ctx, CND_MODE_ENCODE, &program, buffer, sizeof(buffer), bench_io_callback_bitfield, &f);
        cnd_execute(&ctx);
    }
}
BENCHMARK(BM_EncodeBitfields);

static void BM_DecodeBitfields(benchmark::State& state) {
    std::vector<uint8_t> bytecode;
    CompileSchema(
        "packet Flags { uint32 a:5; uint32 b:12; uint32 c:3; uint32 d:12; }", 
        bytecode);
    
    cnd_program program;
    cnd_program_load(&program, bytecode.data(), bytecode.size());
    
    Flags f = { 0x1F, 0xABC, 0x7, 0xFFF };
    
    uint8_t buffer[128];
    cnd_vm_ctx ctx;
    
    // Pre-encode
    cnd_init(&ctx, CND_MODE_ENCODE, &program, buffer, sizeof(buffer), bench_io_callback_bitfield, &f);
    cnd_execute(&ctx);
    size_t encoded_size = ctx.cursor;

    for (auto _ : state) {
        Flags out_f;
        cnd_init(&ctx, CND_MODE_DECODE, &program, buffer, encoded_size, bench_io_callback_bitfield, &out_f);
        cnd_execute(&ctx);
    }
}
BENCHMARK(BM_DecodeBitfields);

// --- Optional Benchmark ---

struct OptionalData {
    uint32_t always;
    uint32_t maybe;
};

static cnd_error_t bench_io_callback_optional(cnd_vm_ctx* ctx, uint16_t key_id, uint8_t type, void* ptr) {
    OptionalData* d = (OptionalData*)ctx->user_ptr;
    switch (key_id) {
        case 0: // always
            if (ctx->mode == CND_MODE_ENCODE) *(uint32_t*)ptr = d->always; else d->always = *(uint32_t*)ptr;
            break;
        case 1: // maybe
            if (ctx->mode == CND_MODE_ENCODE) *(uint32_t*)ptr = d->maybe; else d->maybe = *(uint32_t*)ptr;
            break;
    }
    return CND_ERR_OK;
}

static void BM_EncodeOptional(benchmark::State& state) {
    std::vector<uint8_t> bytecode;
    CompileSchema(
        "packet P { uint32 always; @optional uint32 maybe; }", 
        bytecode);
    
    cnd_program program;
    cnd_program_load(&program, bytecode.data(), bytecode.size());
    
    OptionalData d = { 0x11223344, 0x55667788 };
    
    // Buffer large enough for both
    uint8_t buffer[128];
    cnd_vm_ctx ctx;

    for (auto _ : state) {
        memset(buffer, 0, sizeof(buffer));
        cnd_init(&ctx, CND_MODE_ENCODE, &program, buffer, sizeof(buffer), bench_io_callback_optional, &d);
        cnd_execute(&ctx);
    }
}
BENCHMARK(BM_EncodeOptional);

static void BM_DecodeOptional(benchmark::State& state) {
    std::vector<uint8_t> bytecode;
    CompileSchema(
        "packet P { uint32 always; @optional uint32 maybe; }", 
        bytecode);
    
    cnd_program program;
    cnd_program_load(&program, bytecode.data(), bytecode.size());
    
    OptionalData d = { 0x11223344, 0x55667788 };
    
    uint8_t buffer[128];
    cnd_vm_ctx ctx;
    
    // Pre-encode
    cnd_init(&ctx, CND_MODE_ENCODE, &program, buffer, sizeof(buffer), bench_io_callback_optional, &d);
    cnd_execute(&ctx);
    size_t encoded_size = ctx.cursor;

    for (auto _ : state) {
        OptionalData out_d;
        cnd_init(&ctx, CND_MODE_DECODE, &program, buffer, encoded_size, bench_io_callback_optional, &out_d);
        cnd_execute(&ctx);
    }
}
BENCHMARK(BM_DecodeOptional);

// --- Transform Benchmark ---

struct TransformData {
    float val;
};

static cnd_error_t bench_io_callback_transform(cnd_vm_ctx* ctx, uint16_t key_id, uint8_t type, void* ptr) {
    TransformData* d = (TransformData*)ctx->user_ptr;
    // key 0: val
    if (type == OP_IO_F64) { // VM asks for double for scaling
        if (ctx->mode == CND_MODE_ENCODE) *(double*)ptr = (double)d->val;
        else d->val = (float)*(double*)ptr;
    }
    return CND_ERR_OK;
}

static void BM_EncodeTransform(benchmark::State& state) {
    std::vector<uint8_t> bytecode;
    CompileSchema(
        "packet P { @scale(0.1) @offset(10.0) uint16 val; }", 
        bytecode);
    
    cnd_program program;
    cnd_program_load(&program, bytecode.data(), bytecode.size());
    
    TransformData d = { 25.5f }; // (25.5 - 10) / 0.1 = 155
    
    uint8_t buffer[128];
    cnd_vm_ctx ctx;

    for (auto _ : state) {
        memset(buffer, 0, sizeof(buffer));
        cnd_init(&ctx, CND_MODE_ENCODE, &program, buffer, sizeof(buffer), bench_io_callback_transform, &d);
        cnd_execute(&ctx);
    }
}
BENCHMARK(BM_EncodeTransform);

static void BM_DecodeTransform(benchmark::State& state) {
    std::vector<uint8_t> bytecode;
    CompileSchema(
        "packet P { @scale(0.1) @offset(10.0) uint16 val; }", 
        bytecode);
    
    cnd_program program;
    cnd_program_load(&program, bytecode.data(), bytecode.size());
    
    TransformData d = { 25.5f }; 
    
    uint8_t buffer[128];
    cnd_vm_ctx ctx;
    
    // Pre-encode
    cnd_init(&ctx, CND_MODE_ENCODE, &program, buffer, sizeof(buffer), bench_io_callback_transform, &d);
    cnd_execute(&ctx);
    size_t encoded_size = ctx.cursor;

    for (auto _ : state) {
        TransformData out_d;
        cnd_init(&ctx, CND_MODE_DECODE, &program, buffer, encoded_size, bench_io_callback_transform, &out_d);
        cnd_execute(&ctx);
    }
}
BENCHMARK(BM_DecodeTransform);

// --- CRC Benchmark ---

struct CRCData {
    uint8_t data[1024];
    uint32_t crc;
};

static cnd_error_t bench_io_callback_crc(cnd_vm_ctx* ctx, uint16_t key_id, uint8_t type, void* ptr) {
    CRCData* d = (CRCData*)ctx->user_ptr;
    
    switch (key_id) {
        case 0: // data
             if (type == OP_STR_PRE_U16) {
                 if (ctx->mode == CND_MODE_ENCODE) *(const char**)ptr = (const char*)d->data;
             }
             break;
        case 1: // crc
             if (ctx->mode == CND_MODE_ENCODE) *(uint32_t*)ptr = d->crc; else d->crc = *(uint32_t*)ptr;
             break;
    }
    return CND_ERR_OK;
}

static void BM_EncodeCRC(benchmark::State& state) {
    std::vector<uint8_t> bytecode;
    CompileSchema(
        "packet P { string data prefix u16; @crc(32) uint32 crc; }", 
        bytecode);
    
    cnd_program program;
    cnd_program_load(&program, bytecode.data(), bytecode.size());
    
    CRCData d;
    memset(d.data, 0xAA, 1023);
    d.data[1023] = 0; // Null terminator just in case, though PRE_U16 uses length
    d.crc = 0;
    
    uint8_t buffer[2048];
    cnd_vm_ctx ctx;

    for (auto _ : state) {
        memset(buffer, 0, sizeof(buffer));
        cnd_init(&ctx, CND_MODE_ENCODE, &program, buffer, sizeof(buffer), bench_io_callback_crc, &d);
        cnd_execute(&ctx);
    }
}
BENCHMARK(BM_EncodeCRC);

static void BM_DecodeCRC(benchmark::State& state) {
    std::vector<uint8_t> bytecode;
    CompileSchema(
        "packet P { string data prefix u16; @crc(32) uint32 crc; }", 
        bytecode);
    
    cnd_program program;
    cnd_program_load(&program, bytecode.data(), bytecode.size());
    
    CRCData d;
    memset(d.data, 0xAA, 1023);
    d.data[1023] = 0;
    d.crc = 0;
    
    uint8_t buffer[2048];
    cnd_vm_ctx ctx;
    
    // Pre-encode
    cnd_init(&ctx, CND_MODE_ENCODE, &program, buffer, sizeof(buffer), bench_io_callback_crc, &d);
    cnd_execute(&ctx);
    size_t encoded_size = ctx.cursor;

    for (auto _ : state) {
        CRCData out_d;
        cnd_init(&ctx, CND_MODE_DECODE, &program, buffer, encoded_size, bench_io_callback_crc, &out_d);
        cnd_execute(&ctx);
    }
}
BENCHMARK(BM_DecodeCRC);

// --- String Benchmark ---

struct StringData {
    char str[64];
};

static cnd_error_t bench_io_callback_string(cnd_vm_ctx* ctx, uint16_t key_id, uint8_t type, void* ptr) {
    StringData* d = (StringData*)ctx->user_ptr;
    if (key_id == 0) {
        if (ctx->mode == CND_MODE_ENCODE) *(const char**)ptr = d->str;
        else {
            // For decoding, ptr is (const char*) pointing to the string in the buffer
            const char* src = (const char*)ptr;
            strncpy(d->str, src, 63);
            d->str[63] = 0;
        }
    }
    return CND_ERR_OK;
}

static void BM_EncodeString(benchmark::State& state) {
    std::vector<uint8_t> bytecode;
    CompileSchema("packet P { string s max 64; }", bytecode);
    
    cnd_program program;
    cnd_program_load(&program, bytecode.data(), bytecode.size());
    
    StringData d;
    strcpy(d.str, "Hello World! This is a benchmark string.");
    
    uint8_t buffer[128];
    cnd_vm_ctx ctx;

    for (auto _ : state) {
        memset(buffer, 0, sizeof(buffer));
        cnd_init(&ctx, CND_MODE_ENCODE, &program, buffer, sizeof(buffer), bench_io_callback_string, &d);
        cnd_execute(&ctx);
    }
}
BENCHMARK(BM_EncodeString);

static void BM_DecodeString(benchmark::State& state) {
    std::vector<uint8_t> bytecode;
    CompileSchema("packet P { string s max 64; }", bytecode);
    
    cnd_program program;
    cnd_program_load(&program, bytecode.data(), bytecode.size());
    
    StringData d;
    strcpy(d.str, "Hello World! This is a benchmark string.");
    
    uint8_t buffer[128];
    cnd_vm_ctx ctx;
    
    cnd_init(&ctx, CND_MODE_ENCODE, &program, buffer, sizeof(buffer), bench_io_callback_string, &d);
    cnd_execute(&ctx);
    size_t encoded_size = ctx.cursor;

    for (auto _ : state) {
        StringData out_d;
        cnd_init(&ctx, CND_MODE_DECODE, &program, buffer, encoded_size, bench_io_callback_string, &out_d);
        cnd_execute(&ctx);
    }
}
BENCHMARK(BM_DecodeString);

// --- Endianness Benchmark ---

static void BM_EncodeBigEndian(benchmark::State& state) {
    std::vector<uint8_t> bytecode;
    CompileSchema("packet P { @big_endian uint32 val; }", bytecode);
    
    cnd_program program;
    cnd_program_load(&program, bytecode.data(), bytecode.size());
    
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

// --- Large Array Benchmark ---

static void BM_EncodeLargeArray(benchmark::State& state) {
    std::vector<uint8_t> bytecode;
    CompileSchema("packet P { uint8 data[1024]; }", bytecode);
    
    cnd_program program;
    cnd_program_load(&program, bytecode.data(), bytecode.size());
    
    // We reuse BenchContext but ignore the callback logic for array items 
    // since we just want to measure the loop overhead of the VM for a large fixed array
    // Actually, for fixed arrays of primitives, the VM might optimize? 
    // Currently it loops.
    
    BenchContext bc;
    memset(bc.data.data, 0xAA, 16); // Just dummy
    
    uint8_t buffer[2048];
    cnd_vm_ctx ctx;

    for (auto _ : state) {
        memset(buffer, 0, sizeof(buffer));
        cnd_init(&ctx, CND_MODE_ENCODE, &program, buffer, sizeof(buffer), bench_io_callback_complex, &bc);
        cnd_execute(&ctx);
    }
}
BENCHMARK(BM_EncodeLargeArray);

// --- Setup ---

static void CompileSchema(const char* schema, std::vector<uint8_t>& bytecode) {
    // We use a temporary file for compilation as the compiler API currently works with files
    FILE* f = fopen("bench_temp.cnd", "wb");
    if (!f) {
        fprintf(stderr, "Failed to create bench_temp.cnd\n");
        exit(1);
    }
    fwrite(schema, 1, strlen(schema), f);
    fclose(f);

    if (cnd_compile_file("bench_temp.cnd", "bench_temp.il", 2) != 0) {
        fprintf(stderr, "Compilation failed for schema: %s\n", schema);
        exit(1);
    }

    FILE* il = fopen("bench_temp.il", "rb");
    if (!il) {
        fprintf(stderr, "Failed to open bench_temp.il\n");
        exit(1);
    }
    fseek(il, 0, SEEK_END);
    long size = ftell(il);
    fseek(il, 0, SEEK_SET);
    
    std::vector<uint8_t> file_data(size);
    fread(file_data.data(), 1, size, il);
    fclose(il);

    // Parse header to get bytecode
    // Header: Magic(5) Ver(1) StrCount(2) StrOffset(4) BytecodeOffset(4)
    if (size < 16) {
        fprintf(stderr, "Invalid IL file size\n");
        exit(1);
    }
    uint32_t bytecode_offset = *(uint32_t*)(file_data.data() + 12);
    if (bytecode_offset > size) {
        fprintf(stderr, "Invalid bytecode offset\n");
        exit(1);
    }
    bytecode.assign(file_data.begin() + bytecode_offset, file_data.end());
    
    remove("bench_temp.cnd");
    remove("bench_temp.il");
}

// --- Benchmarks ---

static void BM_EncodeSimple(benchmark::State& state) {
    std::vector<uint8_t> bytecode;
    CompileSchema("packet P { uint32 id; float val; uint8 data[16]; }", bytecode);
    
    cnd_program program;
    cnd_program_load(&program, bytecode.data(), bytecode.size());
    
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
    std::vector<uint8_t> bytecode;
    CompileSchema("packet P { uint32 id; float val; uint8 data[16]; }", bytecode);
    
    cnd_program program;
    cnd_program_load(&program, bytecode.data(), bytecode.size());
    
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

static void BM_EnumEncode(benchmark::State& state) {
    std::vector<uint8_t> bytecode;
    CompileSchema(
        "enum Status : uint8 { Ok = 0, Error = 1, Unknown = 2 }"
        "packet P { Status s; }", 
        bytecode
    );
    
    cnd_program program;
    program.bytecode = bytecode.data();
    program.bytecode_len = bytecode.size();
    
    uint8_t buffer[16];
    cnd_vm_ctx ctx;
    
    // Simple callback that just writes 1 (Error)
    auto cb = [](cnd_vm_ctx* ctx, uint16_t key, uint8_t type, void* ptr) -> cnd_error_t {
        *(uint8_t*)ptr = 1;
        return CND_ERR_OK;
    };
    
    for (auto _ : state) {
        cnd_init(&ctx, CND_MODE_ENCODE, &program, buffer, sizeof(buffer), cb, NULL);
        cnd_execute(&ctx);
    }
}
BENCHMARK(BM_EnumEncode);

BENCHMARK_MAIN();
