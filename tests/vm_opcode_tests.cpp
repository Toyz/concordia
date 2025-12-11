#include "test_common.h"
#include <thread>
#include <vector>
#include <atomic>
#include "../src/vm/vm_internal.h"

TEST_F(ConcordiaTest, AluEncodingBE) {
    g_test_data[0].key = 1; g_test_data[0].u64_val = 0x1234;
    uint8_t il[] = { OP_SET_ENDIAN_BE, OP_IO_U16, 0x01, 0x00 };
    
    memset(m_buffer, 0, sizeof(m_buffer));
    cnd_program_load(&program, il, sizeof(il));
    cnd_init(&ctx, CND_MODE_ENCODE, &program, m_buffer, sizeof(m_buffer), test_io_callback, NULL);
    cnd_execute(&ctx);
    
    EXPECT_EQ(m_buffer[0], 0x12);
    EXPECT_EQ(m_buffer[1], 0x34);
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
    
    memset(m_buffer, 0, sizeof(m_buffer));
    cnd_program_load(&program, il, sizeof(il));
    cnd_init(&ctx, CND_MODE_ENCODE, &program, m_buffer, sizeof(m_buffer), test_io_callback, NULL);
    cnd_execute(&ctx);
    
    // Check U32 LE
    EXPECT_EQ(m_buffer[0], 0x78);
    EXPECT_EQ(m_buffer[1], 0x56);
    EXPECT_EQ(m_buffer[2], 0x34);
    EXPECT_EQ(m_buffer[3], 0x12);
    
    // Check I32 (-1)
    EXPECT_EQ(m_buffer[4], 0xFF);
    EXPECT_EQ(m_buffer[7], 0xFF);
    
    // Check Float (3.14 approx 0x4048F5C3)
    // 0xC3 0xF5 0x48 0x40 (LE)
    EXPECT_EQ(m_buffer[8], 0xC3);
    EXPECT_EQ(m_buffer[11], 0x40);
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
    
    memset(m_buffer, 0, sizeof(m_buffer));
    cnd_program_load(&program, il, sizeof(il));
    cnd_init(&ctx, CND_MODE_ENCODE, &program, m_buffer, sizeof(m_buffer), test_io_callback, NULL);
    cnd_execute(&ctx);
    
    EXPECT_EQ(m_buffer[0], 5); // Length
    EXPECT_EQ(m_buffer[1], 'H');
    EXPECT_EQ(m_buffer[5], 'o');
}

TEST_F(ConcordiaTest, Arrays) {
    g_test_data[0].key = 1; g_test_data[0].u64_val = 0xAA;
    g_test_data[1].key = 3; g_test_data[1].u64_val = 0; // Dummy entry for Array Key
    // Added 0x03, 0x00, 0x00, 0x00 for Count=3 argument to OP_ARR_FIXED (u32)
    uint8_t il[] = { OP_ARR_FIXED, 0x03, 0x00, 0x03, 0x00, 0x00, 0x00, OP_IO_U8, 0x01, 0x00, OP_ARR_END };
    
    memset(m_buffer, 0, sizeof(m_buffer));
    cnd_program_load(&program, il, sizeof(il));
    cnd_init(&ctx, CND_MODE_ENCODE, &program, m_buffer, sizeof(m_buffer), test_io_callback, NULL);
    cnd_error_t err = cnd_execute(&ctx);
    
    EXPECT_EQ(err, CND_ERR_OK);
    EXPECT_EQ(ctx.cursor, 3);
    EXPECT_EQ(m_buffer[0], 0xAA);
    EXPECT_EQ(m_buffer[2], 0xAA);
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
    
    memset(m_buffer, 0, sizeof(m_buffer));
    cnd_program_load(&program, il, sizeof(il));
    cnd_init(&ctx, CND_MODE_ENCODE, &program, m_buffer, sizeof(m_buffer), test_io_callback, NULL);
    cnd_execute(&ctx);
    
    EXPECT_EQ(ctx.cursor, 3);
    EXPECT_EQ(m_buffer[0], 2); // Count
    EXPECT_EQ(m_buffer[1], 0x55);
    EXPECT_EQ(m_buffer[2], 0x55);
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
    
    memset(m_buffer, 0, sizeof(m_buffer));
    cnd_program_load(&program, il, sizeof(il));
    cnd_init(&ctx, CND_MODE_ENCODE, &program, m_buffer, sizeof(m_buffer), test_io_callback, NULL);
    cnd_error_t err = cnd_execute(&ctx);
    
    EXPECT_EQ(err, CND_ERR_OK);
    EXPECT_EQ(ctx.cursor, 1);
    EXPECT_EQ(m_buffer[0], 0x77);
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

    memset(m_buffer, 0, sizeof(m_buffer));
    cnd_program_load(&program, il, sizeof(il));
    cnd_init(&ctx, CND_MODE_ENCODE, &program, m_buffer, sizeof(m_buffer), test_io_callback, NULL);
    cnd_execute(&ctx);

    // Check U64 BE
    EXPECT_EQ(m_buffer[0], 0x11);
    EXPECT_EQ(m_buffer[7], 0x88);

    // Check F64 BE (123.456)
    // Sign(0) Exp(10000000101) Mant(...)
    // 0x405E...
    EXPECT_EQ(m_buffer[8], 0x40);
    EXPECT_EQ(m_buffer[9], 0x5E);
}

TEST_F(ConcordiaTest, NestedArrays) {
    // Array of Arrays
    // struct Row { uint8 cols[2]; }
    // packet Matrix { Row rows[2]; }
    
    CompileAndLoad(
        "struct Row { uint8 cols[2]; }"
        "packet Matrix { Row rows[2]; }"
    );
    
    // Keys after struct prefixing:
    // [0] rows
    // [1] rows.cols
    
    // We need to provide data for 2 rows * 2 cols = 4 items.
    // Since test_io_callback is stateless/simple, we need to be careful.
    // But for fixed arrays, it just calls callback for start.
    // OP_ARR_FIXED (rows) -> Callback(Key 0)
    //   OP_ENTER_STRUCT
    //     OP_ARR_FIXED (rows.cols) -> Callback(Key 1)
    //       OP_IO_U8 -> Callback(Key 1)
    //       OP_IO_U8 -> Callback(Key 1)
    //     OP_ARR_END
    //   OP_EXIT_STRUCT
    //   ... repeat for row 2
    // OP_ARR_END
    
    // We need a smarter callback or just check that it runs without error 
    // and produces correct size.
    // Let's use a simple value for all U8s.
    
    g_test_data[0].key = 0; g_test_data[0].u64_val = 2; // rows count (ignored for fixed, but good practice)
    g_test_data[1].key = 1; g_test_data[1].u64_val = 0x55; // rows.cols data
    
    memset(m_buffer, 0, sizeof(m_buffer));
    cnd_init(&ctx, CND_MODE_ENCODE, &program, m_buffer, sizeof(m_buffer), test_io_callback, NULL);
    cnd_error_t err = cnd_execute(&ctx);
    EXPECT_EQ(err, CND_ERR_OK);
    
    EXPECT_EQ(ctx.cursor, 4);
    EXPECT_EQ(m_buffer[0], 0x55);
    EXPECT_EQ(m_buffer[1], 0x55);
    EXPECT_EQ(m_buffer[2], 0x55);
    EXPECT_EQ(m_buffer[3], 0x55);
}

TEST_F(ConcordiaTest, Bitfields) {
    g_test_data[0].key = 1; g_test_data[0].u64_val = 1;
    g_test_data[1].key = 2; g_test_data[1].u64_val = 1;
    uint8_t il[] = { OP_IO_BIT_U, 0x01, 0x00, 0x01, OP_IO_BIT_U, 0x02, 0x00, 0x01, OP_ALIGN_PAD, 0x06 };
    
    memset(m_buffer, 0, sizeof(m_buffer));
    cnd_program_load(&program, il, sizeof(il));
    cnd_init(&ctx, CND_MODE_ENCODE, &program, m_buffer, sizeof(m_buffer), test_io_callback, NULL);
    cnd_execute(&ctx);
    
    EXPECT_EQ(m_buffer[0], 0x03);
}

TEST_F(ConcordiaTest, BitfieldBoundary) {
    // We want to write 16 bits total (2 bytes) using 3 fields:
    // Field A: 4 bits = 0xF (1111)
    // Field B: 6 bits = 0x2A (101010)
    // Field C: 6 bits = 0x15 (010101)
    
    // Layout in memory (Little Endian filling from LSB):
    // Byte 0: [AAAA BBBB] -> Lower 4 bits of A, Lower 4 bits of B?
    // Actually implementation does: 
    // bit if(val & (1<<i)) ...
    // It fills from bit_offset 0 upwards.
    
    // Field A (4 bits, val 0xF): Bits 0-3 set.
    // Field B (6 bits, val 0x2A = 101010):
    //   Bit 0 (0) -> Pos 4
    //   Bit 1 (1) -> Pos 5
    //   Bit 2 (0) -> Pos 6
    //   Bit 3 (1) -> Pos 7  <- Byte 0 Complete
    //   Bit 4 (0) -> Pos 0 (Byte 1)
    //   Bit 5 (1) -> Pos 1 (Byte 1)
    // Field C (6 bits, val 0x15 = 010101):
    //   Bit 0 (1) -> Pos 2 (Byte 1)
    //   ...
    
    g_test_data[0].key = 1; g_test_data[0].u64_val = 0xF;
    g_test_data[1].key = 2; g_test_data[1].u64_val = 0x2A;
    g_test_data[2].key = 3; g_test_data[2].u64_val = 0x15;

    uint8_t il[] = {
        OP_IO_BIT_U, 0x01, 0x00, 0x04, // 4 bits
        OP_IO_BIT_U, 0x02, 0x00, 0x06, // 6 bits
        OP_IO_BIT_U, 0x03, 0x00, 0x06  // 6 bits
    };

    memset(m_buffer, 0, sizeof(m_buffer));
    cnd_program_load(&program, il, sizeof(il));
    cnd_init(&ctx, CND_MODE_ENCODE, &program, m_buffer, sizeof(m_buffer), test_io_callback, NULL);
    cnd_execute(&ctx);
    
    EXPECT_EQ(ctx.cursor, 2); // Should have advanced to start of 3rd byte? 
    // Actually cursor points to current byte. 
    // 4+6+6 = 16 bits = 2 bytes exactly. 
    // bit_offset should be 0. cursor should be 2.
    
    // Byte 0 analysis:
    // Bits 0-3: 1111 (F)
    // Bits 4-7: First 4 bits of 0x2A (010101) -> 1010 (A)?
    // 0x2A binary is 101010. LSB is 0.
    // bits: 0 1 0 1 0 1
    //       ^ ^ ^ ^
    //       | | | |
    // Pos:  4 5 6 7
    // So Byte 0 high nibble is 1010 (0xA).
    // Byte 0 = 0xAF.
    EXPECT_EQ(m_buffer[0], 0xAF);
    
    // Byte 1 analysis:
    // Remaining bits of B (0x2A): last 2 bits (0, 1) -> 01?
    // 0x2A = 10 1010. Top bits are 10.
    // Pos 0: 0
    // Pos 1: 1
    // Field C (0x15 = 010101) starts at Pos 2.
    // Pos 2: 1
    // Pos 3: 0
    // Pos 4: 1
    // Pos 5: 0
    // Pos 6: 1
    // Pos 7: 0
    // Result binary: 010101 10 -> 0x56
    EXPECT_EQ(m_buffer[1], 0x56);
}

TEST_F(ConcordiaTest, SignedBitfields) {
    // 3 fields of 3 bits each. Total 9 bits (2 bytes).
    // Field 1: 3 (011)
    // Field 2: -1 (111)
    // Field 3: -4 (100)
    
    g_test_data[0].key = 1; g_test_data[0].u64_val = 3;
    g_test_data[1].key = 2; g_test_data[1].u64_val = (uint64_t)-1; // stored as unsigned long long in test struct
    g_test_data[2].key = 3; g_test_data[2].u64_val = (uint64_t)-4;

    uint8_t il[] = {
        OP_IO_BIT_I, 0x01, 0x00, 0x03, // 3 bits
        OP_IO_BIT_I, 0x02, 0x00, 0x03, // 3 bits
        OP_IO_BIT_I, 0x03, 0x00, 0x03  // 3 bits
    };
    
    memset(m_buffer, 0, sizeof(m_buffer));
    cnd_program_load(&program, il, sizeof(il));
    cnd_init(&ctx, CND_MODE_ENCODE, &program, m_buffer, sizeof(m_buffer), test_io_callback, NULL);
    cnd_execute(&ctx);
    
    // Memory Check:
    // Bit stream (LSB first filling):
    // 011 111 100
    // Byte 0 (8 bits): 11 111 011 -> 1111 1011 -> 0xFB ?
    // Bits 0-2: 011 (3)
    // Bits 3-5: 111 (7)
    // Bits 6-7: 00 (First 2 bits of 100, which are 0, 0) -> 00.
    // Wait, 100 is 4. LSB is 0.
    // 011 (3)
    // 111 (-1)
    // 100 (-4)
    // Stream:
    // Pos 0: 1 (3 LSB)
    // Pos 1: 1
    // Pos 2: 0
    // Pos 3: 1 (-1 LSB)
    // Pos 4: 1
    // Pos 5: 1
    // Pos 6: 0 (-4 LSB)
    // Pos 7: 0
    // Pos 8: 1 (-4 MSB)
    // Byte 0: 0011 1011 -> 0x3B.
    // Byte 1: 0000 0001 -> 0x01.
    EXPECT_EQ(m_buffer[0], 0x3B);
    EXPECT_EQ(m_buffer[1], 0x01);
    
    // DECODE Check
    // Reset test data to 0 to ensure we actually read values back
    g_test_data[0].u64_val = 0;
    g_test_data[1].u64_val = 0;
    g_test_data[2].u64_val = 0;
    
    cnd_init(&ctx, CND_MODE_DECODE, &program, m_buffer, sizeof(m_buffer), test_io_callback, NULL);
    cnd_execute(&ctx);
    
    EXPECT_EQ((int64_t)g_test_data[0].u64_val, 3);
    EXPECT_EQ((int64_t)g_test_data[1].u64_val, -1);
    EXPECT_EQ((int64_t)g_test_data[2].u64_val, -4);
}

TEST_F(ConcordiaTest, AlignPad) {
    // Test @pad(n) which inserts explicit padding bits
    // uint8 a : 4;
    // @pad(4)
    // uint8 b;
    // Layout:
    // Byte 0: [aaaa pppp] -> a takes 4 bits, pad takes 4 bits.
    // Byte 1: b starts here.
    
    CompileAndLoad(
        "packet Padding {"
        "  uint8 a : 4;"
        "  @pad(4) uint8 dummy;" // dummy field name required by parser for decorators? 
                                // Wait, @pad is a decorator on a field? 
                                // Parser logic: 
                                // } else if (match_keyword(dec_name_token, "pad")) {
                                //    buf_push(p->target, OP_ALIGN_PAD); ...
                                // }
                                // It pushes opcode immediately. It is attached to the *next* field definition.
                                // So "@pad(4) uint8 b" means "Pad 4 bits, then define field b".
        "  uint8 b;"
        "}"
    );
    
    g_test_data[0].key = 0; g_test_data[0].u64_val = 0xF; // a
    g_test_data[1].key = 1; g_test_data[1].u64_val = 0xAA; // b
    
    memset(m_buffer, 0, sizeof(m_buffer));
    cnd_init(&ctx, CND_MODE_ENCODE, &program, m_buffer, sizeof(m_buffer), test_io_callback, NULL);
    cnd_execute(&ctx);
    
    // Byte 0: 0x0F (Bits 0-3 set). Bits 4-7 are padding (0).
    EXPECT_EQ(m_buffer[0], 0x0F);
    // Byte 1: 0xAA
    EXPECT_EQ(m_buffer[1], 0xAA);
}

TEST_F(ConcordiaTest, AlignFill) {
    // Test @fill which aligns to next byte boundary
    // uint8 a : 3;
    // @fill uint8 b;
    // Layout:
    // Byte 0: [aaa 00000] -> a takes 3 bits. Fill skips remaining 5.
    // Byte 1: b starts here.
    
    CompileAndLoad(
        "packet Filling {"
        "  uint8 a : 3;"
        "  @fill uint8 b;"
        "}"
    );
    
    g_test_data[0].key = 0; g_test_data[0].u64_val = 0x7; // a (111)
    g_test_data[1].key = 1; g_test_data[1].u64_val = 0xFF; // b
    
    memset(m_buffer, 0, sizeof(m_buffer));
    cnd_init(&ctx, CND_MODE_ENCODE, &program, m_buffer, sizeof(m_buffer), test_io_callback, NULL);
    cnd_execute(&ctx);
    
    // Byte 0: 0x07.
    EXPECT_EQ(m_buffer[0], 0x07);
    // Byte 1: 0xFF.
    EXPECT_EQ(m_buffer[1], 0xFF);
}

TEST_F(ConcordiaTest, MemorySafety) {
    g_test_data[0].key = 1; 
    #ifdef _MSC_VER
    strcpy_s(g_test_data[0].string_val, sizeof(g_test_data[0].string_val), "1234567890");
    #else
    strcpy(g_test_data[0].string_val, "1234567890");
    #endif

    uint8_t il[] = { OP_STR_NULL, 0x01, 0x00, 0x05, 0x00 };
    
    memset(m_buffer, 0, sizeof(m_buffer));
    cnd_program_load(&program, il, sizeof(il));
    cnd_init(&ctx, CND_MODE_ENCODE, &program, m_buffer, sizeof(m_buffer), test_io_callback, NULL);
    cnd_execute(&ctx);
    
    EXPECT_EQ(ctx.cursor, 6);
    EXPECT_STREQ((char*)m_buffer, "12345");
}

TEST_F(ConcordiaTest, BufferBounds) {
    // Try to write U16 (2 bytes) into 1-byte m_buffer
    g_test_data[0].key = 1; g_test_data[0].u64_val = 0xFFFF;
    
    uint8_t il[] = { OP_IO_U16, 0x01, 0x00 };
    
    memset(m_buffer, 0, sizeof(m_buffer));
    cnd_program_load(&program, il, sizeof(il));
    // Init with size 1
    cnd_init(&ctx, CND_MODE_ENCODE, &program, m_buffer, 1, test_io_callback, NULL);
    
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
    memset(m_buffer, 0, sizeof(m_buffer));
    cnd_program_load(&program, il, sizeof(il));
    cnd_init(&ctx, CND_MODE_ENCODE, &program, m_buffer, sizeof(m_buffer), test_io_callback, NULL);
    cnd_error_t err = cnd_execute(&ctx);
    EXPECT_EQ(err, CND_ERR_OK);
    
    // Test Fail U8 (Value 21)
    g_test_data[0].u64_val = 21;
    cnd_init(&ctx, CND_MODE_ENCODE, &program, m_buffer, sizeof(m_buffer), test_io_callback, NULL);
    err = cnd_execute(&ctx);
    EXPECT_EQ(err, CND_ERR_VALIDATION);
    
    // Test Fail F32 (Value 1.5)
    g_test_data[0].u64_val = 15; // Fix U8
    g_test_data[1].f64_val = 1.5;
    cnd_init(&ctx, CND_MODE_ENCODE, &program, m_buffer, sizeof(m_buffer), test_io_callback, NULL);
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
    
    memset(m_buffer, 0, sizeof(m_buffer));
    cnd_init(&ctx, CND_MODE_ENCODE, &program, m_buffer, sizeof(m_buffer), test_io_callback, NULL);
    cnd_error_t err = cnd_execute(&ctx);
    
    EXPECT_EQ(err, CND_ERR_CALLBACK);
}

TEST_F(ConcordiaTest, IntegrationPipeline) {
    // 1. Create temporary source file
    const char* src_path = "integration_temp.cnd";
    const char* il_path = "integration_temp.il";

    std::ofstream out(src_path);
    out << "@version(1)\n"
        << "packet Status {\n"
        << "    uint16 voltage;\n"
        << "    @count(3)\n"
        << "    uint8 sensors[3];\n"
        << "    string log until 0x00 max 32;\n"
        << "}\n";
    out.close();

    // 2. Compile and Load generated IL file
    // Compile the example file
    int res = cnd_compile_file(src_path, il_path, 0, 0);
    ASSERT_EQ(res, 0) << "Failed to compile " << src_path;

    FILE* f = fopen(il_path, "rb");
    ASSERT_TRUE(f != NULL) << "Could not open generated IL file";
    
    fseek(f, 0, SEEK_END);
    long fsize = ftell(f);
    fseek(f, 0, SEEK_SET);
    
    std::vector<uint8_t> file_data(fsize);
    fread(file_data.data(), 1, fsize, f);
    fclose(f);
    remove(il_path); // Cleanup
    remove(src_path); // Cleanup source

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

    cnd_program_load(&program, bytecode, bytecode_len);
    cnd_init(&ctx, CND_MODE_DECODE, &program, payload, sizeof(payload), test_io_callback, NULL);
    
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

TEST_F(ConcordiaTest, Constants) {
    // CONST_WRITE U8 (0xAA) -> Buffer
    // CONST_CHECK U8 (0xBB) -> Verify
    
    uint8_t il[] = { 
        OP_CONST_WRITE, OP_IO_U8, 0xAA,
        OP_CONST_CHECK, 0x00, 0x00, OP_IO_U8, 0xBB
    };
    
    // ENCODE: Should write 0xAA and 0xBB
    memset(m_buffer, 0, sizeof(m_buffer));
    cnd_program_load(&program, il, sizeof(il));
    cnd_init(&ctx, CND_MODE_ENCODE, &program, m_buffer, sizeof(m_buffer), test_io_callback, NULL);
    cnd_execute(&ctx);
    
    EXPECT_EQ(m_buffer[0], 0xAA);
    EXPECT_EQ(m_buffer[1], 0xBB);
    
    // DECODE: Should verify 0xBB. Let's give it 0xBC to fail.
    m_buffer[1] = 0xBC;
    cnd_init(&ctx, CND_MODE_DECODE, &program, m_buffer, sizeof(m_buffer), test_io_callback, NULL);
    cnd_error_t err = cnd_execute(&ctx);
    EXPECT_EQ(err, CND_ERR_VALIDATION);
    
    // DECODE: Give correct value
    m_buffer[1] = 0xBB;
    cnd_init(&ctx, CND_MODE_DECODE, &program, m_buffer, sizeof(m_buffer), test_io_callback, NULL);
    err = cnd_execute(&ctx);
    EXPECT_EQ(err, CND_ERR_OK);
}

TEST_F(ConcordiaTest, DecoratorStacking) {
    // Test 1: Endianness + Const
    // @big_endian @const(0x1234) uint16 be_val;
    // Should write 0x12 0x34
    // @little_endian @const(0x5678) uint16 le_val;
    // Should write 0x78 0x56
    
    CompileAndLoad(
        "packet Test1 {"
        "  @big_endian @const(0x1234) uint16 be_val;"
        "  @little_endian @const(0x5678) uint16 le_val;"
        "}"
    );
    
    memset(m_buffer, 0, sizeof(m_buffer));
    cnd_init(&ctx, CND_MODE_ENCODE, &program, m_buffer, sizeof(m_buffer), test_io_callback, NULL);
    cnd_execute(&ctx);
    
    EXPECT_EQ(m_buffer[0], 0x12);
    EXPECT_EQ(m_buffer[1], 0x34);
    EXPECT_EQ(m_buffer[2], 0x78);
    EXPECT_EQ(m_buffer[3], 0x56);
    
    // Test 2: Range + Const (Valid)
    CompileAndLoad(
        "packet Test2 {"
        "  @range(10, 20) @const(15) uint8 valid;"
        "}"
    );
    cnd_init(&ctx, CND_MODE_ENCODE, &program, m_buffer, sizeof(m_buffer), test_io_callback, NULL);
    cnd_error_t err = cnd_execute(&ctx);
    EXPECT_EQ(err, CND_ERR_OK);
    
    // Test 3: Range + Const (Invalid)
    CompileAndLoad(
        "packet Test3 {"
        "  @range(10, 20) @const(25) uint8 invalid;"
        "}"
    );
    cnd_init(&ctx, CND_MODE_ENCODE, &program, m_buffer, sizeof(m_buffer), test_io_callback, NULL);
    err = cnd_execute(&ctx);
    EXPECT_EQ(err, CND_ERR_VALIDATION);
}

TEST_F(ConcordiaTest, MultiRTT) {
    // Round Trip Test: Compile -> Encode -> Decode
    CompileAndLoad(
        "struct Inner { uint8 val; }"
        "packet RTT {"
        "  uint32 id;"
        "  Inner nested;"
        "  uint16 list[] prefix uint8;"
        "  string name prefix uint8;"
        "}"
    );
    
    // Key IDs after struct prefixing:
    // [0] id
    // [1] nested (struct marker)
    // [2] nested.val (prefixed!)
    // [3] list
    // [4] name
    
    clear_test_data();
    g_test_data[0].key = 0; g_test_data[0].u64_val = 0xDEADBEEF; // id
    g_test_data[1].key = 2; g_test_data[1].u64_val = 0x99; // nested.val
    g_test_data[2].key = 3; g_test_data[2].u64_val = 0; // list count
    g_test_data[3].key = 4; // name
    #ifdef _MSC_VER
    strcpy_s(g_test_data[3].string_val, 64, "RTT");
    #else
    strcpy(g_test_data[3].string_val, "RTT");
    #endif
    
    // ENCODE
    memset(m_buffer, 0, sizeof(m_buffer));
    cnd_init(&ctx, CND_MODE_ENCODE, &program, m_buffer, sizeof(m_buffer), test_io_callback, NULL);
    cnd_error_t err = cnd_execute(&ctx);
    EXPECT_EQ(err, CND_ERR_OK);
    
    // DECODE
    // Clear mock data to receive values
    clear_test_data(); 
    // Prepare keys to receive
    g_test_data[0].key = 0; // id
    g_test_data[1].key = 2; // nested.val
    g_test_data[2].key = 3; // list
    g_test_data[3].key = 4; // name
    
    cnd_init(&ctx, CND_MODE_DECODE, &program, m_buffer, sizeof(m_buffer), test_io_callback, NULL);
    err = cnd_execute(&ctx);
    EXPECT_EQ(err, CND_ERR_OK);
    
    EXPECT_EQ(g_test_data[0].u64_val, 0xDEADBEEF);
    EXPECT_EQ(g_test_data[1].u64_val, 0x99);
    EXPECT_EQ(g_test_data[2].u64_val, 0);
    EXPECT_STREQ(g_test_data[3].string_val, "RTT");
}

TEST_F(ConcordiaTest, Scaling) {
    // Key 1: U8 raw=100. Scale 0.1, Offset 5.0 -> Eng 15.0
    // Key 2: F32 raw=3.0. Scale 2.0 -> Eng 6.0
    
    // ENCODE Test
    // We set Eng values in g_test_data.
    g_test_data[0].key = 1; g_test_data[0].f64_val = 15.0;
    g_test_data[1].key = 2; g_test_data[1].f64_val = 6.0;
    
    uint8_t il[] = {
        OP_SCALE_LIN, 
        0x9A, 0x99, 0x99, 0x99, 0x99, 0x99, 0xB9, 0x3F, // 0.1 (double)
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x14, 0x40, // 5.0 (double)
        OP_IO_U8, 0x01, 0x00,
        
        OP_SCALE_LIN,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x40, // 2.0
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // 0.0
        OP_IO_F32, 0x02, 0x00
    };
    
    // Hex for doubles:
    // 0.1 = 0x3FB999999999999A (LE: 9A 99 99 99 99 99 B9 3F)
    // 5.0 = 0x4014000000000000 (LE: 00 00 00 00 00 00 14 40)
    // 2.0 = 0x4000000000000000 (LE: 00 00 00 00 00 00 00 40)
    // 0.0 = 0
    
    memset(m_buffer, 0, sizeof(m_buffer));
    cnd_program_load(&program, il, sizeof(il));
    cnd_init(&ctx, CND_MODE_ENCODE, &program, m_buffer, sizeof(m_buffer), test_io_callback, NULL);
    cnd_execute(&ctx);
    
    // Verify Raw Values in Buffer
    // U8: (15 - 5) / 0.1 = 100 -> 0x64
    EXPECT_EQ(m_buffer[0], 100);
    
    // F32: (6 - 0) / 2 = 3.0 -> 0x40400000 (LE: 00 00 40 40)
    // Buffer index: 1
    EXPECT_EQ(m_buffer[4], 0x40); // MSB of F32
    
    // DECODE Test
    g_test_data[0].f64_val = 0;
    g_test_data[1].f64_val = 0;
    
    cnd_init(&ctx, CND_MODE_DECODE, &program, m_buffer, sizeof(m_buffer), test_io_callback, NULL);
    cnd_execute(&ctx);
    
    // Allow small epsilon for float math
    EXPECT_NEAR(g_test_data[0].f64_val, 15.0, 0.001);
    EXPECT_NEAR(g_test_data[1].f64_val, 6.0, 0.001);
}

TEST_F(ConcordiaTest, IntegerTransform) {
    // Key 1: U8 @add(10). Eng=20 -> Raw=10.
    // Key 2: I16 @mul(2). Eng=100 -> Raw=50.
    // Key 3: I16 @div(2). Eng=25 -> Raw=50.
    // Key 4: U8 @sub(5). Eng=15 -> Raw=20.
    
    g_test_data[0].key = 1; g_test_data[0].u64_val = 20;
    g_test_data[1].key = 2; g_test_data[1].u64_val = 100;
    g_test_data[2].key = 3; g_test_data[2].u64_val = 25;
    g_test_data[3].key = 4; g_test_data[3].u64_val = 15;
    
    uint8_t il[] = {
        OP_TRANS_ADD, 0x0A, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // +10
        OP_IO_U8, 0x01, 0x00,
        
        OP_TRANS_MUL, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // *2
        OP_IO_I16, 0x02, 0x00,
        
        OP_TRANS_DIV, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // /2
        OP_IO_I16, 0x03, 0x00,
        
        OP_TRANS_SUB, 0x05, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // -5
        OP_IO_U8, 0x04, 0x00
    };
    
    memset(m_buffer, 0, sizeof(m_buffer));
    cnd_program_load(&program, il, sizeof(il));
    cnd_init(&ctx, CND_MODE_ENCODE, &program, m_buffer, sizeof(m_buffer), test_io_callback, NULL);
    cnd_execute(&ctx);
    
    EXPECT_EQ(m_buffer[0], 10);
    // I16=50 -> 0x32 0x00 (LE)
    EXPECT_EQ(m_buffer[1], 0x32); EXPECT_EQ(m_buffer[2], 0x00);
    // I16=50
    EXPECT_EQ(m_buffer[3], 0x32); EXPECT_EQ(m_buffer[4], 0x00);
    // U8=20
    EXPECT_EQ(m_buffer[5], 20);
    
    // DECODE
    g_test_data[0].u64_val = 0;
    g_test_data[1].u64_val = 0;
    g_test_data[2].u64_val = 0;
    g_test_data[3].u64_val = 0;
    
    cnd_init(&ctx, CND_MODE_DECODE, &program, m_buffer, sizeof(m_buffer), test_io_callback, NULL);
    cnd_execute(&ctx);
    
    EXPECT_EQ(g_test_data[0].u64_val, 20);
    EXPECT_EQ(g_test_data[1].u64_val, 100);
    EXPECT_EQ(g_test_data[2].u64_val, 25);
    EXPECT_EQ(g_test_data[3].u64_val, 15);
}

TEST_F(ConcordiaTest, OptionalTrailing) {
    CompileAndLoad(
        "packet Optional { uint8 version; @optional uint8 extra; }"
    );
    
    // Key 0: version
    // Key 1: extra
    
    // Test 1: Encode Full
    g_test_data[0].key = 0; g_test_data[0].u64_val = 1;
    g_test_data[1].key = 1; g_test_data[1].u64_val = 5;
    
    memset(m_buffer, 0, sizeof(m_buffer));
    cnd_init(&ctx, CND_MODE_ENCODE, &program, m_buffer, sizeof(m_buffer), test_io_callback, NULL);
    cnd_execute(&ctx);
    
    EXPECT_EQ(ctx.cursor, 2);
    EXPECT_EQ(m_buffer[0], 1);
    EXPECT_EQ(m_buffer[1], 5);
    
    // Test 2: Decode Full
    g_test_data[0].u64_val = 0;
    g_test_data[1].u64_val = 0;
    cnd_init(&ctx, CND_MODE_DECODE, &program, m_buffer, sizeof(m_buffer), test_io_callback, NULL);
    cnd_execute(&ctx);
    EXPECT_EQ(g_test_data[0].u64_val, 1);
    EXPECT_EQ(g_test_data[1].u64_val, 5);
    
    // Test 3: Decode Partial (Truncated m_buffer)
    // Buffer only has 1 byte.
    g_test_data[0].u64_val = 0;
    g_test_data[1].u64_val = 0xFF; // Sentinel
    
    cnd_init(&ctx, CND_MODE_DECODE, &program, m_buffer, 1, test_io_callback, NULL);
    cnd_error_t err = cnd_execute(&ctx);
    EXPECT_EQ(err, CND_ERR_OK); // Should not error on optional
    
    EXPECT_EQ(g_test_data[0].u64_val, 1);
    EXPECT_EQ(g_test_data[1].u64_val, 0); // Callback called with 0
}

TEST_F(ConcordiaTest, CRC32) {
    // Packet with 4 bytes of data and a CRC32
    // Data: "1234" (0x31 0x32 0x33 0x34)
    // CRC-32 (Standard IEEE 802.3) of "1234" is 0x9BE3E0A3
    // Poly: 0x04C11DB7, Init: 0xFFFFFFFF, XorOut: 0xFFFFFFFF, RefIn: true, RefOut: true
    // The compiler defaults for @crc(32) are:
    // Poly: 0x04C11DB7, Init: 0xFFFFFFFF, Xor: 0xFFFFFFFF, Flags: 3 (RefIn | RefOut)
    
    CompileAndLoad(
        "packet Checksum32 {"
        "  uint8 d1; uint8 d2; uint8 d3; uint8 d4;"
        "  @crc(32) uint32 crc;"
        "}"
    );
    
    g_test_data[0].key = 0; g_test_data[0].u64_val = 0x31;
    g_test_data[1].key = 1; g_test_data[1].u64_val = 0x32;
    g_test_data[2].key = 2; g_test_data[2].u64_val = 0x33;
    g_test_data[3].key = 3; g_test_data[3].u64_val = 0x34;
    
    // ENCODE
    memset(m_buffer, 0, sizeof(m_buffer));
    cnd_init(&ctx, CND_MODE_ENCODE, &program, m_buffer, sizeof(m_buffer), test_io_callback, NULL);
    cnd_error_t err = cnd_execute(&ctx);
    EXPECT_EQ(err, CND_ERR_OK);
    
    EXPECT_EQ(ctx.cursor, 8);
    // Check Data
    EXPECT_EQ(m_buffer[0], 0x31);
    EXPECT_EQ(m_buffer[3], 0x34);
    
    // Check CRC (Little Endian)
    // Expected: 0x9BE3E0A3 -> A3 E0 E3 9B
    EXPECT_EQ(m_buffer[4], 0xA3);
    EXPECT_EQ(m_buffer[5], 0xE0);
    EXPECT_EQ(m_buffer[6], 0xE3);
    EXPECT_EQ(m_buffer[7], 0x9B);
    
    // DECODE
    cnd_init(&ctx, CND_MODE_DECODE, &program, m_buffer, sizeof(m_buffer), test_io_callback, NULL);
    err = cnd_execute(&ctx);
    EXPECT_EQ(err, CND_ERR_OK);
}

TEST_F(ConcordiaTest, ComplexIntegration) {
    // Comprehensive test covering:
    // - Primitives (u8-u64, i16, f64)
    // - Endianness (@big_endian, @little_endian)
    // - Constants (@const)
    // - Bitfields
    // - Alignment (@fill)
    // - Variable Arrays (structs)
    // - Strings (prefixed)
    // - Transformations (@scale, @offset)
    // - CRC32
    // - Imports (Nested structs from other files)

    const char* src_path = "complex_all.cnd";
    const char* imp_path = "complex_import.cnd";
    const char* il_path = "complex_all.il";

    // Create Imported File
    std::ofstream out_imp(imp_path);
    out_imp << "struct Imported { u16 imp_val; }\n";
    out_imp.close();

    // Create Main File
    std::ofstream out(src_path);
    out << "@import(\"complex_import.cnd\")\n"
        << "@version(1)\n"
        << "struct Point {\n"
        << "    i16 x;\n"
        << "    i16 y;\n"
        << "}\n"
        << "packet ComplexAll {\n"
        << "    @big_endian u32 magic;\n"
        << "    @little_endian u16 version;\n"
        << "    @const(0xDEADBEEF) u32 signature;\n"
        << "    u8 flags : 4;\n"
        << "    @fill u8 aligned_byte;\n"
        << "    Point points[] prefix u8;\n"
        << "    string name prefix u8;\n"
        << "    @scale(0.1) @offset(5.0) u8 sensor_val;\n" // Changed to u8 for proper scaling test
        << "    Imported imp_data;\n"
        << "    @crc(32) u32 checksum;\n"
        << "}\n";
    out.close();

    // Compile
    int res = cnd_compile_file(src_path, il_path, 0, 0);
    ASSERT_EQ(res, 0) << "Failed to compile " << src_path;

    // Load IL
    FILE* f = fopen(il_path, "rb");
    ASSERT_TRUE(f != NULL);
    fseek(f, 0, SEEK_END);
    long fsize = ftell(f);
    fseek(f, 0, SEEK_SET);
    std::vector<uint8_t> file_data(fsize);
    fread(file_data.data(), 1, fsize, f);
    fclose(f);
    remove(il_path);
    remove(src_path);
    remove(imp_path);

    uint32_t bytecode_offset = *(uint32_t*)(file_data.data() + 12);
    size_t bytecode_len = fsize - bytecode_offset;
    cnd_program_load(&program, file_data.data() + bytecode_offset, bytecode_len);

    // Prepare Data for ENCODE
    clear_test_data();
    
    // Key Mapping (Global String Table Order with struct prefixing):
    // 0: magic
    // 1: version
    // 2: signature
    // 3: flags
    // 4: aligned_byte
    // 5: points
    // 6: points.x
    // 7: points.y
    // 8: name
    // 9: sensor_val
    // 10: imp_data
    // 11: imp_data.imp_val
    // 12: ComplexAll

    g_test_data[0].key = 0; g_test_data[0].u64_val = 0x12345678; // magic
    g_test_data[1].key = 1; g_test_data[1].u64_val = 0x0100;     // version
    g_test_data[2].key = 3; g_test_data[2].u64_val = 0xA;        // flags (1010)
    g_test_data[3].key = 4; g_test_data[3].u64_val = 0xFF;       // aligned_byte
    g_test_data[4].key = 5; g_test_data[4].u64_val = 2;          // count (2 points)
    
    // Point 1
    g_test_data[5].key = 6; g_test_data[5].u64_val = 10;         // points.x
    g_test_data[6].key = 7; g_test_data[6].u64_val = 20;         // points.y
    
    // Point 2
    g_test_data[7].key = 6; g_test_data[7].u64_val = 30;         // points.x
    g_test_data[8].key = 7; g_test_data[8].u64_val = 40;         // points.y
    
    // Name
    g_test_data[9].key = 8; 
    #ifdef _MSC_VER
    strcpy_s(g_test_data[9].string_val, 64, "Test");
    #else
    strcpy(g_test_data[9].string_val, "Test");
    #endif

    // Sensor (Eng 15.0 -> Raw 100)
    g_test_data[10].key = 9; g_test_data[10].f64_val = 15.0;

    // Imported Data
    // Note: OP_ENTER_STRUCT (Key 10) is skipped by test_io_callback logic, so it doesn't consume a tape entry.
    // We only provide data for the field inside the struct.
    g_test_data[11].key = 11; g_test_data[11].u64_val = 0x9999; // imp_data.imp_val

    TestContext tctx = { true, 0 }; // Enable Tape Mode

    memset(m_buffer, 0, sizeof(m_buffer));
    cnd_init(&ctx, CND_MODE_ENCODE, &program, m_buffer, sizeof(m_buffer), test_io_callback, &tctx);
    cnd_error_t err = cnd_execute(&ctx);
    ASSERT_EQ(err, CND_ERR_OK);

    // Verify Buffer Content
    int pos = 0;
    
    // Magic (Big Endian u32 0x12345678) -> 12 34 56 78
    EXPECT_EQ(m_buffer[pos++], 0x12);
    EXPECT_EQ(m_buffer[pos++], 0x34);
    EXPECT_EQ(m_buffer[pos++], 0x56);
    EXPECT_EQ(m_buffer[pos++], 0x78);
    
    // Version (Little Endian u16 0x0100) -> 00 01
    EXPECT_EQ(m_buffer[pos++], 0x00);
    EXPECT_EQ(m_buffer[pos++], 0x01);
    
    // Signature (Const u32 0xDEADBEEF, default LE) -> EF BE AD DE
    EXPECT_EQ(m_buffer[pos++], 0xEF);
    EXPECT_EQ(m_buffer[pos++], 0xBE);
    EXPECT_EQ(m_buffer[pos++], 0xAD);
    EXPECT_EQ(m_buffer[pos++], 0xDE);
    
    // Flags (4 bits 0xA = 1010) + Fill (4 bits 0) -> 0x0A
    EXPECT_EQ(m_buffer[pos++], 0x0A);
    
    // Aligned Byte (0xFF)
    EXPECT_EQ(m_buffer[pos++], 0xFF);
    
    // Count (2)
    EXPECT_EQ(m_buffer[pos++], 0x02);
    
    // Point 1 (x=10, y=20) -> LE i16
    EXPECT_EQ(m_buffer[pos++], 10); EXPECT_EQ(m_buffer[pos++], 0);
    EXPECT_EQ(m_buffer[pos++], 20); EXPECT_EQ(m_buffer[pos++], 0);
    
    // Point 2 (x=30, y=40)
    EXPECT_EQ(m_buffer[pos++], 30); EXPECT_EQ(m_buffer[pos++], 0);
    EXPECT_EQ(m_buffer[pos++], 40); EXPECT_EQ(m_buffer[pos++], 0);
    
    // Name (Prefix u8=4, "Test")
    EXPECT_EQ(m_buffer[pos++], 4);
    EXPECT_EQ(m_buffer[pos++], 'T');
    EXPECT_EQ(m_buffer[pos++], 'e');
    EXPECT_EQ(m_buffer[pos++], 's');
    EXPECT_EQ(m_buffer[pos++], 't');
    
    // Sensor (Raw 100 -> u8)
    EXPECT_EQ(m_buffer[pos++], 100);
    
    // Imported imp_val (u16 0x9999 -> LE 99 99)
    EXPECT_EQ(m_buffer[pos++], 0x99);
    EXPECT_EQ(m_buffer[pos++], 0x99);

    // Checksum (CRC32)
    pos += 4;
    
    EXPECT_EQ(ctx.cursor, pos);

    // DECODE
    clear_test_data();
    tctx.tape_index = 0; // Reset tape
    
    cnd_init(&ctx, CND_MODE_DECODE, &program, m_buffer, sizeof(m_buffer), test_io_callback, &tctx);
    err = cnd_execute(&ctx);
    ASSERT_EQ(err, CND_ERR_OK);
    
    // Verify Decoded Values (using new key mappings with struct prefixing)
    
    EXPECT_EQ(g_test_data[0].key, 0); EXPECT_EQ(g_test_data[0].u64_val, 0x12345678); // magic
    EXPECT_EQ(g_test_data[1].key, 1); EXPECT_EQ(g_test_data[1].u64_val, 0x0100);     // version
    EXPECT_EQ(g_test_data[2].key, 2); EXPECT_EQ(g_test_data[2].u64_val, 0xDEADBEEF); // signature
    EXPECT_EQ(g_test_data[3].key, 3); EXPECT_EQ(g_test_data[3].u64_val, 0xA);        // flags
    EXPECT_EQ(g_test_data[4].key, 4); EXPECT_EQ(g_test_data[4].u64_val, 0xFF);       // aligned_byte
    EXPECT_EQ(g_test_data[5].key, 5); EXPECT_EQ(g_test_data[5].u64_val, 2);          // points count
    
    // Point 1
    EXPECT_EQ(g_test_data[6].key, 6); EXPECT_EQ(g_test_data[6].u64_val, 10);         // points.x
    EXPECT_EQ(g_test_data[7].key, 7); EXPECT_EQ(g_test_data[7].u64_val, 20);         // points.y
    
    // Point 2
    EXPECT_EQ(g_test_data[8].key, 6); EXPECT_EQ(g_test_data[8].u64_val, 30);         // points.x
    EXPECT_EQ(g_test_data[9].key, 7); EXPECT_EQ(g_test_data[9].u64_val, 40);         // points.y
    
    EXPECT_EQ(g_test_data[10].key, 8); EXPECT_STREQ(g_test_data[10].string_val, "Test"); // name
    EXPECT_EQ(g_test_data[11].key, 9); EXPECT_NEAR(g_test_data[11].f64_val, 15.0, 0.001); // sensor_val
    
    // Imported
    // imp_data (Key 10) is skipped by callback in DECODE too.
    // So next entry is imp_data.imp_val (Key 11).
    EXPECT_EQ(g_test_data[12].key, 11); EXPECT_EQ(g_test_data[12].u64_val, 0x9999); // imp_data.imp_val
}

struct ThreadData {
    int i;
    int j;
};

// Callback that verifies values during decoding
static cnd_error_t verify_cb(cnd_vm_ctx* ctx, uint16_t key, uint8_t type, void* ptr) {
    ThreadData* data = (ThreadData*)ctx->user_ptr;
    uint32_t val = 0;

    if (type == OP_IO_U32) {
        val = *(uint32_t*)ptr;
    } else {
        return CND_ERR_OK;
    }

    const char* key_name = cnd_get_key_name(ctx->program, key);
    if (!key_name) return CND_ERR_INVALID_OP;

    if (strcmp(key_name, "x") == 0) {
        if (val != (uint32_t)data->i) return CND_ERR_VALIDATION;
    } else if (strcmp(key_name, "y") == 0) {
        if (val != (uint32_t)data->j) return CND_ERR_VALIDATION;
    }

    return CND_ERR_OK;
}

// Callback that provides values during encoding
static cnd_error_t encode_cb(cnd_vm_ctx* ctx, uint16_t key, uint8_t type, void* ptr) {
    (void)type;
    ThreadData* data = (ThreadData*)ctx->user_ptr;
    const char* key_name = cnd_get_key_name(ctx->program, key);
    if (!key_name) return CND_ERR_INVALID_OP;

    if (strcmp(key_name, "x") == 0) {
        *(uint32_t*)ptr = (uint32_t)data->i;
    } else if (strcmp(key_name, "y") == 0) {
        *(uint32_t*)ptr = (uint32_t)data->j;
    }
    return CND_ERR_OK;
}

class ConcurrencyTest : public ConcordiaTest {
protected:
    void SetUp() override {
        ConcordiaTest::SetUp();
        
        // We need a valid IL image.
        // Let's use the compiler to generate it to be safe and easy.
        // Use 'packet' to ensure bytecode is emitted to the global scope.
        const char* source = "packet Point { uint32 x; uint32 y; };";
        CompileAndLoad(source);
    }
};

TEST_F(ConcurrencyTest, ParallelExecution) {
    const int NUM_THREADS = 20;
    const int ITERATIONS_PER_THREAD = 100;
    std::vector<std::thread> threads;
    std::atomic<int> success_count(0);

    for (int i = 0; i < NUM_THREADS; ++i) {
        threads.emplace_back([this, &success_count, i]() {
            ThreadData tdata;
            tdata.i = i;
            for (int j = 0; j < ITERATIONS_PER_THREAD; ++j) {
                tdata.j = j;
                
                cnd_vm_ctx ctx;
                uint8_t m_buffer[8] = {0}; // 2 * uint32
                
                // Initialize m_buffer with data for decoding
                // x = i, y = j
                // Little endian
                m_buffer[0] = i & 0xFF;
                m_buffer[1] = (i >> 8) & 0xFF;
                m_buffer[2] = (i >> 16) & 0xFF; m_buffer[3] = (i >> 24) & 0xFF;
                
                m_buffer[4] = j & 0xFF;
                m_buffer[5] = (j >> 8) & 0xFF;
                m_buffer[6] = (j >> 16) & 0xFF; m_buffer[7] = (j >> 24) & 0xFF;

                cnd_init(&ctx, CND_MODE_DECODE, &this->program, m_buffer, sizeof(m_buffer), verify_cb, &tdata);
                
                cnd_error_t err = cnd_execute(&ctx);
                if (err == CND_ERR_OK) {
                    success_count++;
                }
            }
        });
    }

    for (auto& t : threads) {
        t.join();
    }

    EXPECT_EQ(success_count, NUM_THREADS * ITERATIONS_PER_THREAD);
}

TEST_F(ConcurrencyTest, ParallelEncoding) {
    const int NUM_THREADS = 20;
    const int ITERATIONS_PER_THREAD = 100;
    std::vector<std::thread> threads;
    std::atomic<int> success_count(0);

    for (int i = 0; i < NUM_THREADS; ++i) {
        threads.emplace_back([this, &success_count, i]() {
            ThreadData tdata;
            tdata.i = i;
            for (int j = 0; j < ITERATIONS_PER_THREAD; ++j) {
                tdata.j = j;

                cnd_vm_ctx ctx;
                uint8_t m_buffer[8] = {0};
                
                cnd_init(&ctx, CND_MODE_ENCODE, &this->program, m_buffer, sizeof(m_buffer), encode_cb, &tdata);
                
                cnd_error_t err = cnd_execute(&ctx);
                if (err == CND_ERR_OK) {
                    // Verify m_buffer content
                    uint32_t x = m_buffer[0] | (m_buffer[1] << 8) | (m_buffer[2] << 16) | (m_buffer[3] << 24);
                    uint32_t y = m_buffer[4] | (m_buffer[5] << 8) | (m_buffer[6] << 16) | (m_buffer[7] << 24);
                    
                    if (x == (uint32_t)i && y == (uint32_t)j) {
                        success_count++;
                    }
                }
            }
        });
    }

    for (auto& t : threads) {
        t.join();
    }

    EXPECT_EQ(success_count, NUM_THREADS * ITERATIONS_PER_THREAD);
}

TEST_F(ConcurrencyTest, ParallelRoundTrip) {
    const int NUM_THREADS = 20;
    const int ITERATIONS_PER_THREAD = 100;
    std::vector<std::thread> threads;
    std::atomic<int> success_count(0);

    for (int i = 0; i < NUM_THREADS; ++i) {
        threads.emplace_back([this, &success_count, i]() {
            ThreadData tdata;
            tdata.i = i;
            
            for (int j = 0; j < ITERATIONS_PER_THREAD; ++j) {
                tdata.j = j;
                
                // Buffer for the round trip
                uint8_t m_buffer[8] = {0};
                
                // 1. Encode: Write i, j into the m_buffer
                cnd_vm_ctx ctx_enc;
                cnd_init(&ctx_enc, CND_MODE_ENCODE, &this->program, m_buffer, sizeof(m_buffer), encode_cb, &tdata);
                if (cnd_execute(&ctx_enc) != CND_ERR_OK) continue;

                // 2. Decode & Verify: Read from m_buffer and compare against i, j in tdata
                cnd_vm_ctx ctx_dec;
                cnd_init(&ctx_dec, CND_MODE_DECODE, &this->program, m_buffer, sizeof(m_buffer), verify_cb, &tdata);
                
                if (cnd_execute(&ctx_dec) == CND_ERR_OK) {
                    success_count++;
                }
            }
        });
    }

    for (auto& t : threads) {
        t.join();
    }

    EXPECT_EQ(success_count, NUM_THREADS * ITERATIONS_PER_THREAD);
}

class VmAluTest : public ::testing::Test {
protected:
    cnd_vm_ctx ctx;
    cnd_program program;
    uint8_t data[1024];
    uint8_t bytecode[1024];
    
    void SetUp() override {
        memset(&ctx, 0, sizeof(ctx));
        memset(&program, 0, sizeof(program));
        memset(data, 0, sizeof(data));
        memset(bytecode, 0, sizeof(bytecode));
        
        program.bytecode = bytecode;
        program.bytecode_len = sizeof(bytecode);
        
        cnd_init(&ctx, CND_MODE_ENCODE, &program, data, sizeof(data), NULL, NULL);
    }

    void Run(size_t len) {
        program.bytecode_len = len;
        ASSERT_EQ(cnd_execute(&ctx), CND_ERR_OK);
    }
};

TEST_F(VmAluTest, StackPushPop) {
    // PUSH_IMM 42, POP
    uint8_t* p = bytecode;
    *p++ = OP_PUSH_IMM;
    *(uint64_t*)p = 42; p += 8;
    *p++ = OP_POP;
    
    Run(p - bytecode);
    EXPECT_EQ(ctx.expr_sp, 0);
}

TEST_F(VmAluTest, BitwiseAnd) {
    // PUSH 0x0F, PUSH 0x03, AND -> 0x03
    uint8_t* p = bytecode;
    *p++ = OP_PUSH_IMM; *(uint64_t*)p = 0x0F; p += 8;
    *p++ = OP_PUSH_IMM; *(uint64_t*)p = 0x03; p += 8;
    *p++ = OP_BIT_AND;
    
    Run(p - bytecode);
    EXPECT_EQ(ctx.expr_sp, 1);
    EXPECT_EQ(ctx.expr_stack[0], 0x03);
}

TEST_F(VmAluTest, ComparisonEq) {
    // PUSH 10, PUSH 10, EQ -> 1
    uint8_t* p = bytecode;
    *p++ = OP_PUSH_IMM; *(uint64_t*)p = 10; p += 8;
    *p++ = OP_PUSH_IMM; *(uint64_t*)p = 10; p += 8;
    *p++ = OP_EQ;
    
    Run(p - bytecode);
    EXPECT_EQ(ctx.expr_sp, 1);
    EXPECT_EQ(ctx.expr_stack[0], 1);
}

TEST_F(VmAluTest, ComparisonNeq) {
    // PUSH 10, PUSH 20, NEQ -> 1
    uint8_t* p = bytecode;
    *p++ = OP_PUSH_IMM; *(uint64_t*)p = 10; p += 8;
    *p++ = OP_PUSH_IMM; *(uint64_t*)p = 20; p += 8;
    *p++ = OP_NEQ;
    
    Run(p - bytecode);
    EXPECT_EQ(ctx.expr_sp, 1);
    EXPECT_EQ(ctx.expr_stack[0], 1);
}

TEST_F(VmAluTest, LogicalNot) {
    // PUSH 0, NOT -> 1
    uint8_t* p = bytecode;
    *p++ = OP_PUSH_IMM; *(uint64_t*)p = 0; p += 8;
    *p++ = OP_LOG_NOT;
    
    Run(p - bytecode);
    EXPECT_EQ(ctx.expr_sp, 1);
    EXPECT_EQ(ctx.expr_stack[0], 1);
    
    // Reset
    ctx.expr_sp = 0;
    ctx.ip = 0;
    
    // PUSH 1, NOT -> 0
    p = bytecode;
    *p++ = OP_PUSH_IMM; *(uint64_t*)p = 1; p += 8;
    *p++ = OP_LOG_NOT;
    
    Run(p - bytecode);
    EXPECT_EQ(ctx.expr_sp, 1);
    EXPECT_EQ(ctx.expr_stack[0], 0);
}

TEST_F(VmAluTest, JumpIfNot) {
    uint8_t bc[] = {
        OP_PUSH_IMM, 0, 0, 0, 0, 0, 0, 0, 0,
        OP_JUMP_IF_NOT, 9, 0, 0, 0, // Jump 9 bytes forward
        OP_PUSH_IMM, 1, 0, 0, 0, 0, 0, 0, 0, // Skipped
        OP_PUSH_IMM, 2, 0, 0, 0, 0, 0, 0, 0  // Target
    };
    
    cnd_program_load(&program, bc, sizeof(bc));
    ctx.program = &program;
    
    EXPECT_EQ(cnd_execute(&ctx), CND_ERR_OK);
    
    // Stack should have 2 (and not 1)
    EXPECT_EQ(ctx.expr_sp, 1);
    EXPECT_EQ(ctx.expr_stack[0], 2);
}

TEST_F(VmAluTest, JumpIfNotTaken) {
    uint8_t bc[] = {
        OP_PUSH_IMM, 1, 0, 0, 0, 0, 0, 0, 0,
        OP_JUMP_IF_NOT, 9, 0, 0, 0, // Jump 9 bytes forward
        OP_PUSH_IMM, 1, 0, 0, 0, 0, 0, 0, 0, // Executed
        OP_PUSH_IMM, 2, 0, 0, 0, 0, 0, 0, 0  // Executed
    };
    
    cnd_program_load(&program, bc, sizeof(bc));
    ctx.program = &program;
    
    EXPECT_EQ(cnd_execute(&ctx), CND_ERR_OK);
    
    // Stack should have 1, 2
    EXPECT_EQ(ctx.expr_sp, 2);
    EXPECT_EQ(ctx.expr_stack[0], 1);
    EXPECT_EQ(ctx.expr_stack[1], 2);
}

TEST_F(ConcordiaTest, PolyCrashRepro) {
    const char* schema = "packet P { @poly(0.5, 2.0, 1.5) uint8 val; }";
    CompileAndLoad(schema);
    
    // Setup data
    // The compiler assigns keys sequentially. 'val' should be key 0.
    g_test_data[0].key = 0; 
    g_test_data[0].f64_val = 100.0; 
    
    memset(m_buffer, 0, sizeof(m_buffer));
    cnd_init(&ctx, CND_MODE_ENCODE, &program, m_buffer, sizeof(m_buffer), test_io_callback, &m_tctx);
    
    cnd_error_t err = cnd_execute(&ctx);
    EXPECT_EQ(err, CND_ERR_OK);
}
