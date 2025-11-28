#include "test_common.h"

TEST_F(ConcordiaTest, AluEncodingBE) {
    g_test_data[0].key = 1; g_test_data[0].u64_val = 0x1234;
    uint8_t il[] = { OP_SET_ENDIAN_BE, OP_IO_U16, 0x01, 0x00 };
    
    memset(buffer, 0, sizeof(buffer));
    cnd_program_load(&program, il, sizeof(il));
    cnd_init(&ctx, CND_MODE_ENCODE, &program, buffer, sizeof(buffer), test_io_callback, NULL);
    cnd_execute(&ctx);
    
    EXPECT_EQ(buffer[0], 0x12);
    EXPECT_EQ(buffer[1], 0x34);
}

TEST_F(ConcordiaTest, Primitives) {
    // Key 1: U32 = 0x12345678
    g_test_data[0].key = 1; g_test_data[0].u64_val = 0x12345678;
    // Key 2: I32 = -1 (0xFFFFFFFF)
    g_test_data[1].key = 2; g_test_data[1].u64_val = (uint64_t)-1;
    // Key 3: Float = 3.14
    g_test_data[2].key = 3; g_test_data[2].f64_val = 3.14;
    
    uint8_t il[] = { 
        OP_SET_ENDIAN_LE, 
        OP_IO_U32, 0x01, 0x00,
        OP_IO_I32, 0x02, 0x00,
        OP_IO_F32, 0x03, 0x00
    };
    
    memset(buffer, 0, sizeof(buffer));
    cnd_program_load(&program, il, sizeof(il));
    cnd_init(&ctx, CND_MODE_ENCODE, &program, buffer, sizeof(buffer), test_io_callback, NULL);
    cnd_execute(&ctx);
    
    // Check U32 LE
    EXPECT_EQ(buffer[0], 0x78);
    EXPECT_EQ(buffer[1], 0x56);
    EXPECT_EQ(buffer[2], 0x34);
    EXPECT_EQ(buffer[3], 0x12);
    
    // Check I32 (-1)
    EXPECT_EQ(buffer[4], 0xFF);
    EXPECT_EQ(buffer[7], 0xFF);
    
    // Check Float (3.14 approx 0x4048F5C3)
    // 0xC3 0xF5 0x48 0x40 (LE)
    EXPECT_EQ(buffer[8], 0xC3);
    EXPECT_EQ(buffer[11], 0x40);
}

TEST_F(ConcordiaTest, Strings) {
    g_test_data[0].key = 1; 
    #ifdef _MSC_VER
    strcpy_s(g_test_data[0].string_val, 64, "Hello");
    #else
    strcpy(g_test_data[0].string_val, "Hello");
    #endif

    // Test Prefixed U8 String
    uint8_t il[] = { OP_STR_PRE_U8, 0x01, 0x00 };
    
    memset(buffer, 0, sizeof(buffer));
    cnd_program_load(&program, il, sizeof(il));
    cnd_init(&ctx, CND_MODE_ENCODE, &program, buffer, sizeof(buffer), test_io_callback, NULL);
    cnd_execute(&ctx);
    
    EXPECT_EQ(buffer[0], 5); // Length
    EXPECT_EQ(buffer[1], 'H');
    EXPECT_EQ(buffer[5], 'o');
}

TEST_F(ConcordiaTest, Arrays) {
    g_test_data[0].key = 1; g_test_data[0].u64_val = 0xAA;
    g_test_data[1].key = 3; g_test_data[1].u64_val = 0; // Dummy entry for Array Key
    // Added 0x03, 0x00, 0x00, 0x00 for Count=3 argument to OP_ARR_FIXED (u32)
    uint8_t il[] = { OP_ARR_FIXED, 0x03, 0x00, 0x03, 0x00, 0x00, 0x00, OP_IO_U8, 0x01, 0x00, OP_ARR_END };
    
    memset(buffer, 0, sizeof(buffer));
    cnd_program_load(&program, il, sizeof(il));
    cnd_init(&ctx, CND_MODE_ENCODE, &program, buffer, sizeof(buffer), test_io_callback, NULL);
    cnd_error_t err = cnd_execute(&ctx);
    
    EXPECT_EQ(err, CND_ERR_OK);
    EXPECT_EQ(ctx.cursor, 3);
    EXPECT_EQ(buffer[0], 0xAA);
    EXPECT_EQ(buffer[2], 0xAA);
}

TEST_F(ConcordiaTest, VariableArrays) {
    // Key 1: Count = 2
    g_test_data[0].key = 1; g_test_data[0].u64_val = 2;
    // Key 2: Data = 0x55
    g_test_data[1].key = 2; g_test_data[1].u64_val = 0x55; 

    // ARR_PRE_U8 (Key 1)
    //   IO_U8 (Key 2)
    // ARR_END
    uint8_t il[] = { 
        OP_ARR_PRE_U8, 0x01, 0x00, 
            OP_IO_U8, 0x02, 0x00, 
        OP_ARR_END 
    };
    
    memset(buffer, 0, sizeof(buffer));
    cnd_program_load(&program, il, sizeof(il));
    cnd_init(&ctx, CND_MODE_ENCODE, &program, buffer, sizeof(buffer), test_io_callback, NULL);
    cnd_execute(&ctx);
    
    EXPECT_EQ(ctx.cursor, 3);
    EXPECT_EQ(buffer[0], 2); // Count
    EXPECT_EQ(buffer[1], 0x55);
    EXPECT_EQ(buffer[2], 0x55);
}

TEST_F(ConcordiaTest, NestedStructs) {
    // Key 1: Struct Key (Ignored by default callback logic but verified return OK)
    // Key 2: U8 = 0x77
    g_test_data[0].key = 2; g_test_data[0].u64_val = 0x77;

    uint8_t il[] = {
        OP_ENTER_STRUCT, 0x01, 0x00,
        OP_IO_U8, 0x02, 0x00,
        OP_EXIT_STRUCT
    };
    
    memset(buffer, 0, sizeof(buffer));
    cnd_program_load(&program, il, sizeof(il));
    cnd_init(&ctx, CND_MODE_ENCODE, &program, buffer, sizeof(buffer), test_io_callback, NULL);
    cnd_error_t err = cnd_execute(&ctx);
    
    EXPECT_EQ(err, CND_ERR_OK);
    EXPECT_EQ(ctx.cursor, 1);
    EXPECT_EQ(buffer[0], 0x77);
}

TEST_F(ConcordiaTest, F64AndI64) {
    // Key 1: U64 = 0x1122334455667788
    g_test_data[0].key = 1; g_test_data[0].u64_val = 0x1122334455667788ULL;
    // Key 2: F64 = 123.456
    // 123.456 double (Hex representation approx: 0x405EDD2F1A9FBE77)
    g_test_data[1].key = 2; g_test_data[1].f64_val = 123.456;

    uint8_t il[] = {
        OP_SET_ENDIAN_BE,
        OP_IO_U64, 0x01, 0x00,
        OP_IO_F64, 0x02, 0x00
    };

    memset(buffer, 0, sizeof(buffer));
    cnd_program_load(&program, il, sizeof(il));
    cnd_init(&ctx, CND_MODE_ENCODE, &program, buffer, sizeof(buffer), test_io_callback, NULL);
    cnd_execute(&ctx);

    // Check U64 BE
    EXPECT_EQ(buffer[0], 0x11);
    EXPECT_EQ(buffer[7], 0x88);

    // Check F64 BE (123.456)
    // Sign(0) Exp(10000000101) Mant(...)
    // 0x405E...
    EXPECT_EQ(buffer[8], 0x40);
    EXPECT_EQ(buffer[9], 0x5E);
}

TEST_F(ConcordiaTest, NestedArrays) {
    // Array of Arrays
    // struct Row { uint8 cols[2]; }
    // packet Matrix { Row rows[2]; }
    
    CompileAndLoad(
        "struct Row { uint8 cols[2]; }"
        "packet Matrix { Row rows[2]; }"
    );
    
    // Keys:
    // cols: 0
    // rows: 1
    
    // We need to provide data for 2 rows * 2 cols = 4 items.
    // Since test_io_callback is stateless/simple, we need to be careful.
    // But for fixed arrays, it just calls callback for start.
    // OP_ARR_FIXED (rows) -> Callback(Key 1)
    //   OP_ENTER_STRUCT
    //     OP_ARR_FIXED (cols) -> Callback(Key 0)
    //       OP_IO_U8 -> Callback(Key 0)
    //       OP_IO_U8 -> Callback(Key 0)
    //     OP_ARR_END
    //   OP_EXIT_STRUCT
    //   ... repeat for row 2
    // OP_ARR_END
    
    // We need a smarter callback or just check that it runs without error 
    // and produces correct size.
    // Let's use a simple value for all U8s.
    
    g_test_data[0].key = 0; g_test_data[0].u64_val = 0x55; // cols data
    g_test_data[1].key = 1; g_test_data[1].u64_val = 2; // rows count (ignored for fixed, but good practice)
    
    memset(buffer, 0, sizeof(buffer));
    cnd_init(&ctx, CND_MODE_ENCODE, &program, buffer, sizeof(buffer), test_io_callback, NULL);
    cnd_error_t err = cnd_execute(&ctx);
    EXPECT_EQ(err, CND_ERR_OK);
    
    EXPECT_EQ(ctx.cursor, 4);
    EXPECT_EQ(buffer[0], 0x55);
    EXPECT_EQ(buffer[1], 0x55);
    EXPECT_EQ(buffer[2], 0x55);
    EXPECT_EQ(buffer[3], 0x55);
}
