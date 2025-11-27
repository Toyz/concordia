#include "test_common.h"

TEST_F(ConcordiaTest, MemorySafety) {
    g_test_data[0].key = 1; 
    #ifdef _MSC_VER
    strcpy_s(g_test_data[0].string_val, sizeof(g_test_data[0].string_val), "1234567890");
    #else
    strcpy(g_test_data[0].string_val, "1234567890");
    #endif

    uint8_t il[] = { OP_STR_NULL, 0x01, 0x00, 0x05, 0x00 };
    
    memset(buffer, 0, sizeof(buffer));
    cnd_program_load(&program, il, sizeof(il));
    cnd_init(&ctx, CND_MODE_ENCODE, &program, buffer, sizeof(buffer), test_io_callback, NULL);
    cnd_execute(&ctx);
    
    EXPECT_EQ(ctx.cursor, 6);
    EXPECT_STREQ((char*)buffer, "12345");
}

TEST_F(ConcordiaTest, BufferBounds) {
    // Try to write U16 (2 bytes) into 1-byte buffer
    g_test_data[0].key = 1; g_test_data[0].u64_val = 0xFFFF;
    
    uint8_t il[] = { OP_IO_U16, 0x01, 0x00 };
    
    memset(buffer, 0, sizeof(buffer));
    cnd_program_load(&program, il, sizeof(il));
    // Init with size 1
    cnd_init(&ctx, CND_MODE_ENCODE, &program, buffer, 1, test_io_callback, NULL);
    
    cnd_error_t err = cnd_execute(&ctx);
    EXPECT_EQ(err, CND_ERR_OOB);
}

TEST_F(ConcordiaTest, RangeCheck) {
    // 1. U8 in range [10, 20]
    // 2. F32 in range [0.0, 1.0]
    
    g_test_data[0].key = 1; g_test_data[0].u64_val = 15; // OK
    g_test_data[1].key = 2; g_test_data[1].f64_val = 0.5; // OK
    
    // Float binary rep (Little Endian): 
    // 0.0 = 0x00000000
    // 1.0 = 0x3F800000 -> 00 00 80 3F
    
    uint8_t il[] = {
        OP_IO_U8, 0x01, 0x00,
        OP_RANGE_CHECK, OP_IO_U8, 10, 20,
        
        OP_IO_F32, 0x02, 0x00,
        OP_RANGE_CHECK, OP_IO_F32, 
        0x00, 0x00, 0x00, 0x00, // 0.0
        0x00, 0x00, 0x80, 0x3F  // 1.0
    };
    
    // Test OK
    memset(buffer, 0, sizeof(buffer));
    cnd_program_load(&program, il, sizeof(il));
    cnd_init(&ctx, CND_MODE_ENCODE, &program, buffer, sizeof(buffer), test_io_callback, NULL);
    cnd_error_t err = cnd_execute(&ctx);
    EXPECT_EQ(err, CND_ERR_OK);
    
    // Test Fail U8 (Value 21)
    g_test_data[0].u64_val = 21;
    cnd_init(&ctx, CND_MODE_ENCODE, &program, buffer, sizeof(buffer), test_io_callback, NULL);
    err = cnd_execute(&ctx);
    EXPECT_EQ(err, CND_ERR_VALIDATION);
    
    // Test Fail F32 (Value 1.5)
    g_test_data[0].u64_val = 15; // Fix U8
    g_test_data[1].f64_val = 1.5;
    cnd_init(&ctx, CND_MODE_ENCODE, &program, buffer, sizeof(buffer), test_io_callback, NULL);
    err = cnd_execute(&ctx);
    EXPECT_EQ(err, CND_ERR_VALIDATION);
}

TEST_F(ConcordiaTest, CallbackError) {
    // Test that if callback returns error, VM stops
    CompileAndLoad("packet Err { uint8 val; }");
    
    // Set callback to return error
    // We can't easily change the function pointer, but we can make it fail based on key.
    // Key 0 is val.
    // In test_io_callback: if (idx == -1) return CND_ERR_CALLBACK;
    // So if we don't provide data for Key 0, it returns CND_ERR_CALLBACK.
    
    clear_test_data(); // No data
    
    memset(buffer, 0, sizeof(buffer));
    cnd_init(&ctx, CND_MODE_ENCODE, &program, buffer, sizeof(buffer), test_io_callback, NULL);
    cnd_error_t err = cnd_execute(&ctx);
    
    EXPECT_EQ(err, CND_ERR_CALLBACK);
}
