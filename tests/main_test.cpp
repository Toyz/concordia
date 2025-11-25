#include <gtest/gtest.h>
#include "concordia.h"

// --- Test Mock Data ---

typedef struct {
    uint16_t key;
    uint64_t u64_val;
    char string_val[64]; 
} test_data_entry;

#define MAX_TEST_ENTRIES 16
test_data_entry g_test_data[MAX_TEST_ENTRIES];

void clear_test_data() {
    memset(g_test_data, 0, sizeof(g_test_data));
    for(int i=0; i<MAX_TEST_ENTRIES; i++) g_test_data[i].key = 0xFFFF;
}

// C-compatible callback for the VM
extern "C" cnd_error_t test_io_callback(cnd_vm_ctx* ctx, uint16_t key_id, uint8_t type, void* ptr) {
    int idx = -1;
    // Find existing
    for(int i=0; i<MAX_TEST_ENTRIES; i++) {
        if (g_test_data[i].key == key_id) {
            idx = i;
            break;
        }
    }

    if (ctx->mode == CND_MODE_ENCODE) {
        if (idx == -1) return CND_ERR_CALLBACK;
        
        switch(type) {
            case OP_IO_U8:  *(uint8_t*)ptr = (uint8_t)g_test_data[idx].u64_val; break;
            case OP_IO_U16: *(uint16_t*)ptr = (uint16_t)g_test_data[idx].u64_val; break;
            case OP_IO_BIT_U: *(uint64_t*)ptr = g_test_data[idx].u64_val; break;
            
            case OP_STR_NULL:
                *(const char**)ptr = g_test_data[idx].string_val;
                break;

            default: return CND_ERR_INVALID_OP;
        }
    } else {
        // DECODE MODE
        if (idx == -1) {
             for(int i=0; i<MAX_TEST_ENTRIES; i++) {
                if (g_test_data[i].key == 0xFFFF) { 
                     g_test_data[i].key = key_id;
                     idx = i;
                     break;
                }
            }
        }
        
        if (idx == -1) return CND_ERR_CALLBACK; // No space left
        
        switch(type) {
            case OP_IO_U8:  g_test_data[idx].u64_val = *(uint8_t*)ptr; break;
            case OP_IO_U16: g_test_data[idx].u64_val = *(uint16_t*)ptr; break;
            case OP_IO_BIT_U: g_test_data[idx].u64_val = *(uint64_t*)ptr; break;
            
            case OP_STR_NULL:
                #ifdef _MSC_VER
                strncpy_s(g_test_data[idx].string_val, sizeof(g_test_data[idx].string_val), (const char*)ptr, 63);
                #else
                strncpy(g_test_data[idx].string_val, (const char*)ptr, 63);
                #endif
                break;

            default: return CND_ERR_INVALID_OP;
        }
    }
    return CND_ERR_OK;
}

class ConcordiaTest : public ::testing::Test {
protected:
    void SetUp() override {
        clear_test_data();
    }
    
    uint8_t buffer[64];
    cnd_vm_ctx ctx;
};

TEST_F(ConcordiaTest, AluEncodingBE) {
    g_test_data[0].key = 1; g_test_data[0].u64_val = 0x1234;
    uint8_t il[] = { OP_SET_ENDIAN_BE, OP_IO_U16, 0x01, 0x00 };
    
    memset(buffer, 0, sizeof(buffer));
    cnd_init(&ctx, CND_MODE_ENCODE, il, sizeof(il), buffer, sizeof(buffer), test_io_callback, NULL);
    cnd_execute(&ctx);
    
    EXPECT_EQ(buffer[0], 0x12);
    EXPECT_EQ(buffer[1], 0x34);
}

TEST_F(ConcordiaTest, Bitfields) {
    g_test_data[0].key = 1; g_test_data[0].u64_val = 1;
    g_test_data[1].key = 2; g_test_data[1].u64_val = 1;
    uint8_t il[] = { OP_IO_BIT_U, 0x01, 0x00, 0x01, OP_IO_BIT_U, 0x02, 0x00, 0x01, OP_ALIGN_PAD, 0x06 };
    
    memset(buffer, 0, sizeof(buffer));
    cnd_init(&ctx, CND_MODE_ENCODE, il, sizeof(il), buffer, sizeof(buffer), test_io_callback, NULL);
    cnd_execute(&ctx);
    
    EXPECT_EQ(buffer[0], 0x03);
}

TEST_F(ConcordiaTest, MemorySafety) {
    g_test_data[0].key = 1; 
    #ifdef _MSC_VER
    strcpy_s(g_test_data[0].string_val, sizeof(g_test_data[0].string_val), "1234567890");
    #else
    strcpy(g_test_data[0].string_val, "1234567890");
    #endif

    uint8_t il[] = { OP_STR_NULL, 0x01, 0x00, 0x05, 0x00 };
    
    memset(buffer, 0, sizeof(buffer));
    cnd_init(&ctx, CND_MODE_ENCODE, il, sizeof(il), buffer, sizeof(buffer), test_io_callback, NULL);
    cnd_execute(&ctx);
    
    EXPECT_EQ(ctx.cursor, 6);
    EXPECT_STREQ((char*)buffer, "12345");
}

TEST_F(ConcordiaTest, Arrays) {
    g_test_data[0].key = 1; g_test_data[0].u64_val = 0xAA;
    uint8_t il[] = { OP_ARR_FIXED, 0x03, 0x00, OP_IO_U8, 0x01, 0x00, OP_ARR_END };
    
    memset(buffer, 0, sizeof(buffer));
    cnd_init(&ctx, CND_MODE_ENCODE, il, sizeof(il), buffer, sizeof(buffer), test_io_callback, NULL);
    cnd_execute(&ctx);
    
    EXPECT_EQ(ctx.cursor, 3);
    EXPECT_EQ(buffer[0], 0xAA);
    EXPECT_EQ(buffer[2], 0xAA);
}

TEST_F(ConcordiaTest, IntegrationPipeline) {
    // 1. Load generated IL file (Assumes cndc ran successfully)
    FILE* f = fopen("example.il", "rb");
    ASSERT_TRUE(f != NULL) << "Could not open example.il. Run cndc first!";
    
    fseek(f, 0, SEEK_END);
    long fsize = ftell(f);
    fseek(f, 0, SEEK_SET);
    
    std::vector<uint8_t> file_data(fsize);
    fread(file_data.data(), 1, fsize, f);
    fclose(f);

    // Parse Header
    uint32_t bytecode_offset = *(uint32_t*)(file_data.data() + 12);
    const uint8_t* bytecode = file_data.data() + bytecode_offset;
    size_t bytecode_len = fsize - bytecode_offset;

    // 2. Mock Payload
    uint8_t payload[] = { 
        0x34, 0x12, 
        0x01, 0x02, 0x03, 
        'H', 'i', 0x00 
    };

    cnd_init(&ctx, CND_MODE_DECODE, bytecode, bytecode_len, payload, sizeof(payload), test_io_callback, NULL);
    
    cnd_error_t err = cnd_execute(&ctx);
    ASSERT_EQ(err, CND_ERR_OK);

    // 3. Verify Data
    int found_volt = 0;
    int found_log = 0;
    
    for(int i=0; i<MAX_TEST_ENTRIES; i++) {
        if (g_test_data[i].key == 0) { // Voltage
            EXPECT_EQ(g_test_data[i].u64_val, 0x1234);
            found_volt = 1;
        }
        if (g_test_data[i].key == 1) { // Sensors
             EXPECT_EQ(g_test_data[i].u64_val, 0x03); 
        }
        if (g_test_data[i].key == 2) { // Log
            EXPECT_STREQ(g_test_data[i].string_val, "Hi");
            found_log = 1;
        }
    }
    
    EXPECT_TRUE(found_volt);
    EXPECT_TRUE(found_log);
}
