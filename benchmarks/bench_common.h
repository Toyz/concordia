#pragma once
#include <benchmark/benchmark.h>
#include "../include/concordia.h"
#include "../include/compiler.h"
#include <vector>
#include <cstring>

void CompileSchema(const char* schema, std::vector<uint8_t>& bytecode);

struct BenchData {
    uint32_t id;
    float val;
    uint8_t data[16];
};

cnd_error_t bench_io_callback(cnd_vm_ctx* ctx, uint16_t key_id, uint8_t type, void* ptr);

struct BenchContext {
    BenchData data;
    int array_idx;
};

cnd_error_t bench_io_callback_complex(cnd_vm_ctx* ctx, uint16_t key_id, uint8_t type, void* ptr);
