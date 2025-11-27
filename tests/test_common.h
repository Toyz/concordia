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
        
        // Store full IL image
        il_buffer = file_data;
        
        // Load using new API
        cnd_error_t err = cnd_program_load_il(&program, il_buffer.data(), il_buffer.size());
        ASSERT_EQ(err, CND_ERR_OK) << "Failed to load IL image";
        
        remove(tmp_src);
        remove(tmp_il);
    }
};

#endif // TEST_COMMON_H
