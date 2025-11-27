#ifndef TEST_COMMON_H
#define TEST_COMMON_H

#include <gtest/gtest.h>
#include <fstream>
#include <vector>
#include <cstdio>
#include "concordia.h"
#include "compiler.h"

// --- Test Mock Data ---

typedef struct {
    uint16_t key;
    uint64_t u64_val;
    double f64_val;
    char string_val[64]; 
} test_data_entry;

#define MAX_TEST_ENTRIES 64

extern test_data_entry g_test_data[MAX_TEST_ENTRIES];

void clear_test_data();

typedef struct {
    bool use_tape;
    int tape_index;
} TestContext;

// C-compatible callback for the VM
extern "C" cnd_error_t test_io_callback(cnd_vm_ctx* ctx, uint16_t key_id, uint8_t type, void* ptr);

class ConcordiaTest : public ::testing::Test {
protected:
    void SetUp() override {
        clear_test_data();
    }
    
    uint8_t buffer[64];
    cnd_vm_ctx ctx;
    cnd_program program;
    std::vector<uint8_t> il_buffer; // To hold IL data during tests
    TestContext tctx;

    bool Compile(const char* source) {
        const char* tmp_src = "temp_test.cnd";
        const char* tmp_il = "temp_test.il";
        
        std::ofstream out(tmp_src);
        out << source;
        out.close();
        
        int res = cnd_compile_file(tmp_src, tmp_il, 0);
        
        remove(tmp_src);
        remove(tmp_il);
        
        return res == 0;
    }

    void CompileAndLoad(const char* source) {
        tctx.use_tape = false;
        tctx.tape_index = 0;
        
        const char* tmp_src = "temp_test.cnd";
        const char* tmp_il = "temp_test.il";
        
        std::ofstream out(tmp_src);
        out << source;
        out.close();
        
        int res = cnd_compile_file(tmp_src, tmp_il, 0);
        ASSERT_EQ(res, 0) << "Compilation failed";
        
        std::ifstream f(tmp_il, std::ios::binary | std::ios::ate);
        ASSERT_TRUE(f.good()) << "IL file not created";
        long size = f.tellg();
        f.seekg(0, std::ios::beg);
        
        std::vector<uint8_t> file_data(size);
        f.read((char*)file_data.data(), size);
        f.close();
        
        // Parse Header (Skip to bytecode)
        // Header: Magic(5) Ver(1) StrCount(2) StrOffset(4) BytecodeOffset(4)
        ASSERT_GT(size, 16);
        uint32_t bytecode_offset = *(uint32_t*)(file_data.data() + 12);
        ASSERT_LT(bytecode_offset, size);
        
        size_t bytecode_len = size - bytecode_offset;
        il_buffer.assign(file_data.begin() + bytecode_offset, file_data.end());
        
        cnd_program_load(&program, il_buffer.data(), il_buffer.size());
        
        remove(tmp_src);
        remove(tmp_il);
    }
};

#endif // TEST_COMMON_H
