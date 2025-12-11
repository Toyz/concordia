#include "test_common.h"
#include "../src/vm/vm_internal.h"
#include <cmath>

class SafetyPerfTest : public ConcordiaTest {};

// --- Safety Tests ---

TEST_F(SafetyPerfTest, DivByZero_Integer) {
    // 10 / 0
    uint8_t il[] = { 
        OP_PUSH_IMM, 10, 0, 0, 0, 0, 0, 0, 0, // Push 10
        OP_PUSH_IMM, 0, 0, 0, 0, 0, 0, 0, 0,  // Push 0
        OP_DIV                                // 10 / 0
    };
    
    cnd_program_load(&program, il, sizeof(il));
    cnd_init(&ctx, CND_MODE_ENCODE, &program, m_buffer, sizeof(m_buffer), test_io_callback, NULL);
    cnd_error_t err = cnd_execute(&ctx);
    
    EXPECT_EQ(err, CND_ERR_ARITHMETIC);
}

TEST_F(SafetyPerfTest, ModByZero_Integer) {
    // 10 % 0
    uint8_t il[] = { 
        OP_PUSH_IMM, 10, 0, 0, 0, 0, 0, 0, 0, // Push 10
        OP_PUSH_IMM, 0, 0, 0, 0, 0, 0, 0, 0,  // Push 0
        OP_MOD                                // 10 % 0
    };
    
    cnd_program_load(&program, il, sizeof(il));
    cnd_init(&ctx, CND_MODE_ENCODE, &program, m_buffer, sizeof(m_buffer), test_io_callback, NULL);
    cnd_error_t err = cnd_execute(&ctx);
    
    EXPECT_EQ(err, CND_ERR_ARITHMETIC);
}

TEST_F(SafetyPerfTest, DivByZero_Float) {
    // 10.0 / 0.0
    double a = 10.0;
    double b = 0.0;
    uint64_t a_bits, b_bits;
    memcpy(&a_bits, &a, 8);
    memcpy(&b_bits, &b, 8);

    uint8_t il[19]; // 1 + 8 + 1 + 8 + 1
    il[0] = OP_PUSH_IMM; memcpy(il+1, &a_bits, 8);
    il[9] = OP_PUSH_IMM; memcpy(il+10, &b_bits, 8);
    il[18] = OP_FDIV;

    cnd_program_load(&program, il, sizeof(il));
    cnd_init(&ctx, CND_MODE_ENCODE, &program, m_buffer, sizeof(m_buffer), test_io_callback, NULL);
    cnd_error_t err = cnd_execute(&ctx);
    
    EXPECT_EQ(err, CND_ERR_ARITHMETIC);
}

TEST_F(SafetyPerfTest, SqrtNegative) {
    // sqrt(-1.0)
    double a = -1.0;
    uint64_t a_bits;
    memcpy(&a_bits, &a, 8);

    uint8_t il[10];
    il[0] = OP_PUSH_IMM; memcpy(il+1, &a_bits, 8);
    il[9] = OP_SQRT;

    cnd_program_load(&program, il, sizeof(il));
    cnd_init(&ctx, CND_MODE_ENCODE, &program, m_buffer, sizeof(m_buffer), test_io_callback, NULL);
    cnd_error_t err = cnd_execute(&ctx);
    
    EXPECT_EQ(err, CND_ERR_ARITHMETIC);
}

TEST_F(SafetyPerfTest, ArrayDynamicOverflow) {
    // OP_ARR_DYNAMIC with count > UINT32_MAX
    // We need to mock the callback to return a large value for OP_CTX_QUERY
    
    g_test_data[0].key = 1; // Ref Key
    g_test_data[0].u64_val = 0x100000000ULL; // 4GB + 1 (Overflows u32)
    
    uint8_t il[] = { 
        OP_ARR_DYNAMIC, 0x02, 0x00, 0x01, 0x00, // Key 2, Ref Key 1
        OP_IO_U8, 0x02, 0x00,
        OP_ARR_END
    };

    cnd_program_load(&program, il, sizeof(il));
    cnd_init(&ctx, CND_MODE_ENCODE, &program, m_buffer, sizeof(m_buffer), test_io_callback, NULL);
    cnd_error_t err = cnd_execute(&ctx);
    
    EXPECT_EQ(err, CND_ERR_ARITHMETIC);
}

TEST_F(SafetyPerfTest, JumpUnderflow) {
    // Jump backwards by more than IP
    // IP starts at 0.
    // OP_JUMP (5 bytes)
    // We want to jump back 10 bytes.
    // Offset = -10 (0xFFFFFFF6)
    
    uint8_t il[] = { 
        OP_JUMP, 0xF6, 0xFF, 0xFF, 0xFF // Jump -10
    };

    cnd_program_load(&program, il, sizeof(il));
    cnd_init(&ctx, CND_MODE_ENCODE, &program, m_buffer, sizeof(m_buffer), test_io_callback, NULL);
    cnd_error_t err = cnd_execute(&ctx);
    
    EXPECT_EQ(err, CND_ERR_OOB);
}

// --- Performance Tests (Correctness Verification) ---

TEST_F(SafetyPerfTest, OptimizedBitRead) {
    // Verify that byte-aligned reads still work correctly
    // Write 0x12345678 (BE) to buffer
    m_buffer[0] = 0x12; m_buffer[1] = 0x34; m_buffer[2] = 0x56; m_buffer[3] = 0x78;
    
    // Read 32 bits (aligned)
    uint8_t il[] = { 
        OP_SET_ENDIAN_BE,
        OP_IO_BIT_U, 0x01, 0x00, 32 // Key 1, 32 bits
    };

    cnd_program_load(&program, il, sizeof(il));
    cnd_init(&ctx, CND_MODE_DECODE, &program, m_buffer, sizeof(m_buffer), test_io_callback, NULL);
    cnd_error_t err = cnd_execute(&ctx);
    
    EXPECT_EQ(err, CND_ERR_OK);
    EXPECT_EQ(g_test_data[0].u64_val, 0x12345678);
}

TEST_F(SafetyPerfTest, OptimizedBitWrite) {
    // Verify that byte-aligned writes still work correctly
    g_test_data[0].key = 1;
    g_test_data[0].u64_val = 0xDEADBEEF;
    
    // Write 32 bits (aligned)
    uint8_t il[] = { 
        OP_SET_ENDIAN_BE,
        OP_IO_BIT_U, 0x01, 0x00, 32 // Key 1, 32 bits
    };

    memset(m_buffer, 0, sizeof(m_buffer));
    cnd_program_load(&program, il, sizeof(il));
    cnd_init(&ctx, CND_MODE_ENCODE, &program, m_buffer, sizeof(m_buffer), test_io_callback, NULL);
    cnd_error_t err = cnd_execute(&ctx);
    
    EXPECT_EQ(err, CND_ERR_OK);
    EXPECT_EQ(m_buffer[0], 0xDE);
    EXPECT_EQ(m_buffer[1], 0xAD);
    EXPECT_EQ(m_buffer[2], 0xBE);
    EXPECT_EQ(m_buffer[3], 0xEF);
}

TEST_F(SafetyPerfTest, OptimizedPadding) {
    // Write 3 bits, then pad 5 bits (to align to byte)
    // Then write 0xFF
    
    g_test_data[0].key = 1; g_test_data[0].u64_val = 0x7; // 111
    
    uint8_t il[] = { 
        OP_IO_BIT_U, 0x01, 0x00, 3, // Write 3 bits (111) -> 00000111 (LE: 11100000 in byte?)
                                    // Wait, LE bit writing:
                                    // bit 0 -> mask 1 (0x01)
                                    // bit 1 -> mask 2 (0x02)
                                    // bit 2 -> mask 4 (0x04)
                                    // Val 7 (111).
                                    // Byte becomes 0x07.
                                    // Offset becomes 3.
        OP_ALIGN_PAD, 5,            // Pad 5 bits. Offset 3+5=8 -> 0. Cursor++.
        OP_IO_U8, 0x02, 0x00        // Write 0xFF (Key 2)
    };
    
    g_test_data[1].key = 2; g_test_data[1].u64_val = 0xFF;

    memset(m_buffer, 0, sizeof(m_buffer));
    cnd_program_load(&program, il, sizeof(il));
    cnd_init(&ctx, CND_MODE_ENCODE, &program, m_buffer, sizeof(m_buffer), test_io_callback, NULL);
    cnd_error_t err = cnd_execute(&ctx);
    
    EXPECT_EQ(err, CND_ERR_OK);
    EXPECT_EQ(m_buffer[0], 0x07); // First byte has 3 bits set
    EXPECT_EQ(m_buffer[1], 0xFF); // Second byte is 0xFF
    EXPECT_EQ(ctx.cursor, 2);
}

// --- New Feature Tests ---

TEST_F(SafetyPerfTest, FloatComparison_Eq) {
    double a = 10.5;
    double b = 10.5;
    uint64_t a_bits, b_bits;
    memcpy(&a_bits, &a, 8);
    memcpy(&b_bits, &b, 8);

    uint8_t il[19];
    il[0] = OP_PUSH_IMM; memcpy(il+1, &a_bits, 8);
    il[9] = OP_PUSH_IMM; memcpy(il+10, &b_bits, 8);
    il[18] = OP_EQ_F;

    cnd_program_load(&program, il, sizeof(il));
    cnd_init(&ctx, CND_MODE_ENCODE, &program, m_buffer, sizeof(m_buffer), NULL, NULL);
    EXPECT_EQ(cnd_execute(&ctx), CND_ERR_OK);
    
    uint64_t res = ctx.expr_stack[ctx.expr_sp - 1];
    ctx.expr_sp--;
    EXPECT_EQ(res, 1);
}

TEST_F(SafetyPerfTest, FloatComparison_Neq) {
    double a = 10.5;
    double b = 10.6;
    uint64_t a_bits, b_bits;
    memcpy(&a_bits, &a, 8);
    memcpy(&b_bits, &b, 8);

    uint8_t il[19];
    il[0] = OP_PUSH_IMM; memcpy(il+1, &a_bits, 8);
    il[9] = OP_PUSH_IMM; memcpy(il+10, &b_bits, 8);
    il[18] = OP_NEQ_F;

    cnd_program_load(&program, il, sizeof(il));
    cnd_init(&ctx, CND_MODE_ENCODE, &program, m_buffer, sizeof(m_buffer), NULL, NULL);
    EXPECT_EQ(cnd_execute(&ctx), CND_ERR_OK);
    
    uint64_t res = ctx.expr_stack[ctx.expr_sp - 1];
    ctx.expr_sp--;
    EXPECT_EQ(res, 1);
}

TEST_F(SafetyPerfTest, FloatComparison_Gt) {
    double a = 20.0;
    double b = 10.0;
    uint64_t a_bits, b_bits;
    memcpy(&a_bits, &a, 8);
    memcpy(&b_bits, &b, 8);

    uint8_t il[19];
    il[0] = OP_PUSH_IMM; memcpy(il+1, &a_bits, 8); // Push 20
    il[9] = OP_PUSH_IMM; memcpy(il+10, &b_bits, 8); // Push 10
    il[18] = OP_GT_F; // 20 > 10 ?

    cnd_program_load(&program, il, sizeof(il));
    cnd_init(&ctx, CND_MODE_ENCODE, &program, m_buffer, sizeof(m_buffer), NULL, NULL);
    EXPECT_EQ(cnd_execute(&ctx), CND_ERR_OK);
    
    uint64_t res = ctx.expr_stack[ctx.expr_sp - 1];
    ctx.expr_sp--;
    EXPECT_EQ(res, 1);
}

TEST_F(SafetyPerfTest, StringLookup) {
    // Create a fake program with a string table
    // "CNDIL" + Ver(1) + StrCount(2) + StrOff(16) + BCOff(24)
    // Strings: "Hello\0World\0" (6 + 6 = 12 bytes)
    // Bytecode: OP_NOOP (1 byte)
    
    uint8_t image[100];
    memset(image, 0, sizeof(image));
    memcpy(image, "CNDIL", 5);
    image[5] = 1;
    
    uint16_t str_count = 2;
    uint32_t str_off = 16;
    uint32_t bc_off = 16 + 12; // 28
    
    memcpy(image + 6, &str_count, 2);
    memcpy(image + 8, &str_off, 4);
    memcpy(image + 12, &bc_off, 4);
    
    memcpy(image + 16, "Hello\0World\0", 12);
    image[28] = OP_NOOP;
    
    cnd_program prog;
    EXPECT_EQ(cnd_program_load_il(&prog, image, sizeof(image)), CND_ERR_OK);
    
    EXPECT_EQ(cnd_get_key_id(&prog, "Hello"), 0);
    EXPECT_EQ(cnd_get_key_id(&prog, "World"), 1);
    EXPECT_EQ(cnd_get_key_id(&prog, "Foo"), 0xFFFF);
}
