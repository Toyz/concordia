#include "bench_common.h"

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
    std::vector<uint8_t> il_image;
    CompileSchema(
        "packet P { string data prefix u16; @crc(32) uint32 crc; }", 
        il_image);
    
    cnd_program program;
    cnd_program_load_il(&program, il_image.data(), il_image.size());
    
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
    std::vector<uint8_t> il_image;
    CompileSchema(
        "packet P { string data prefix u16; @crc(32) uint32 crc; }", 
        il_image);
    
    cnd_program program;
    cnd_program_load_il(&program, il_image.data(), il_image.size());
    
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
    std::vector<uint8_t> il_image;
    CompileSchema("packet P { string s max 64; }", il_image);
    
    cnd_program program;
    cnd_program_load_il(&program, il_image.data(), il_image.size());
    
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
    std::vector<uint8_t> il_image;
    CompileSchema("packet P { string s max 64; }", il_image);
    
    cnd_program program;
    cnd_program_load_il(&program, il_image.data(), il_image.size());
    
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

static void BM_EnumEncode(benchmark::State& state) {
    std::vector<uint8_t> il_image;
    CompileSchema(
        "enum Status : uint8 { Ok = 0, Error = 1, Unknown = 2 }"
        "packet P { Status s; }", 
        il_image
    );
    
    cnd_program program;
    cnd_program_load_il(&program, il_image.data(), il_image.size());
    
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

// --- String Array Benchmark ---

struct StringArrayBenchContext {
    const char* strings[10];
    int count;
    int current_idx;
};

static cnd_error_t bench_string_array_callback(cnd_vm_ctx* ctx, uint16_t key_id, uint8_t type, void* ptr) {
    StringArrayBenchContext* bc = (StringArrayBenchContext*)ctx->user_ptr;
    
    if (type == OP_ARR_FIXED) {
        bc->current_idx = 0;
        return CND_ERR_OK;
    }
    if (type == OP_ARR_END) return CND_ERR_OK;

    if (type == OP_STR_NULL || type == OP_STR_PRE_U8) {
        if (ctx->mode == CND_MODE_ENCODE) {
            if (bc->current_idx < bc->count) {
                *(const char**)ptr = bc->strings[bc->current_idx];
                bc->current_idx++;
            }
        } else {
            // Decode: Just consume
        }
    }
    return CND_ERR_OK;
}

static void BM_StringArray_Encode(benchmark::State& state) {
    const char* schema = R"(
        packet BenchPacket {
            @count(5)
            string items[] until 0;
        }
    )";
    
    std::vector<uint8_t> il_image;
    CompileSchema(schema, il_image);
    
    cnd_program program;
    cnd_program_load_il(&program, il_image.data(), il_image.size());
    
    StringArrayBenchContext bc;
    bc.strings[0] = "StringOne";
    bc.strings[1] = "StringTwo";
    bc.strings[2] = "StringThree";
    bc.strings[3] = "StringFour";
    bc.strings[4] = "StringFive";
    bc.count = 5;
    
    uint8_t buffer[256];
    cnd_vm_ctx ctx;
    
    for (auto _ : state) {
        bc.current_idx = 0;
        cnd_init(&ctx, CND_MODE_ENCODE, &program, buffer, sizeof(buffer), bench_string_array_callback, &bc);
        cnd_execute(&ctx);
    }
}
BENCHMARK(BM_StringArray_Encode);

static void BM_StringArray_Decode(benchmark::State& state) {
    const char* schema = R"(
        packet BenchPacket {
            @count(5)
            string items[] until 0;
        }
    )";
    
    std::vector<uint8_t> il_image;
    CompileSchema(schema, il_image);
    
    cnd_program program;
    cnd_program_load_il(&program, il_image.data(), il_image.size());
    
    StringArrayBenchContext bc;
    bc.strings[0] = "StringOne";
    bc.strings[1] = "StringTwo";
    bc.strings[2] = "StringThree";
    bc.strings[3] = "StringFour";
    bc.strings[4] = "StringFive";
    bc.count = 5;
    
    uint8_t buffer[256];
    cnd_vm_ctx ctx;
    
    // Pre-encode to get valid buffer
    bc.current_idx = 0;
    cnd_init(&ctx, CND_MODE_ENCODE, &program, buffer, sizeof(buffer), bench_string_array_callback, &bc);
    cnd_execute(&ctx);
    size_t encoded_size = ctx.cursor;
    
    for (auto _ : state) {
        bc.current_idx = 0;
        cnd_init(&ctx, CND_MODE_DECODE, &program, buffer, encoded_size, bench_string_array_callback, &bc);
        cnd_execute(&ctx);
    }
}
BENCHMARK(BM_StringArray_Decode);
