#include "bench_common.h"
#include <cstdio>
#include <cstdlib>

void CompileSchema(const char* schema, std::vector<uint8_t>& bytecode) {
    // We use a temporary file for compilation as the compiler API currently works with files
    FILE* f = fopen("bench_temp.cnd", "wb");
    if (!f) {
        fprintf(stderr, "Failed to create bench_temp.cnd\n");
        exit(1);
    }
    fwrite(schema, 1, strlen(schema), f);
    fclose(f);

    if (cnd_compile_file("bench_temp.cnd", "bench_temp.il", 0, 0) != 0) {
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

    // Return full IL image
    bytecode = file_data;
    
    remove("bench_temp.cnd");
    remove("bench_temp.il");
}

cnd_error_t bench_io_callback(cnd_vm_ctx* ctx, uint16_t key_id, uint8_t type, void* ptr) {
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

cnd_error_t bench_io_callback_complex(cnd_vm_ctx* ctx, uint16_t key_id, uint8_t type, void* ptr) {
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
