#include "test_common.h"

TEST_F(ConcordiaTest, Bitfields) {
    g_test_data[0].key = 1; g_test_data[0].u64_val = 1;
    g_test_data[1].key = 2; g_test_data[1].u64_val = 1;
    uint8_t il[] = { OP_IO_BIT_U, 0x01, 0x00, 0x01, OP_IO_BIT_U, 0x02, 0x00, 0x01, OP_ALIGN_PAD, 0x06 };
    
    memset(buffer, 0, sizeof(buffer));
    cnd_program_load(&program, il, sizeof(il));
    cnd_init(&ctx, CND_MODE_ENCODE, &program, buffer, sizeof(buffer), test_io_callback, NULL);
    cnd_execute(&ctx);
    
    EXPECT_EQ(buffer[0], 0x03);
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

    memset(buffer, 0, sizeof(buffer));
    cnd_program_load(&program, il, sizeof(il));
    cnd_init(&ctx, CND_MODE_ENCODE, &program, buffer, sizeof(buffer), test_io_callback, NULL);
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
    EXPECT_EQ(buffer[0], 0xAF);
    
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
    EXPECT_EQ(buffer[1], 0x56);
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
    
    memset(buffer, 0, sizeof(buffer));
    cnd_program_load(&program, il, sizeof(il));
    cnd_init(&ctx, CND_MODE_ENCODE, &program, buffer, sizeof(buffer), test_io_callback, NULL);
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
    EXPECT_EQ(buffer[0], 0x3B);
    EXPECT_EQ(buffer[1], 0x01);
    
    // DECODE Check
    // Reset test data to 0 to ensure we actually read values back
    g_test_data[0].u64_val = 0;
    g_test_data[1].u64_val = 0;
    g_test_data[2].u64_val = 0;
    
    cnd_init(&ctx, CND_MODE_DECODE, &program, buffer, sizeof(buffer), test_io_callback, NULL);
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
    
    memset(buffer, 0, sizeof(buffer));
    cnd_init(&ctx, CND_MODE_ENCODE, &program, buffer, sizeof(buffer), test_io_callback, NULL);
    cnd_execute(&ctx);
    
    // Byte 0: 0x0F (Bits 0-3 set). Bits 4-7 are padding (0).
    EXPECT_EQ(buffer[0], 0x0F);
    // Byte 1: 0xAA
    EXPECT_EQ(buffer[1], 0xAA);
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
    
    memset(buffer, 0, sizeof(buffer));
    cnd_init(&ctx, CND_MODE_ENCODE, &program, buffer, sizeof(buffer), test_io_callback, NULL);
    cnd_execute(&ctx);
    
    // Byte 0: 0x07.
    EXPECT_EQ(buffer[0], 0x07);
    // Byte 1: 0xFF.
    EXPECT_EQ(buffer[1], 0xFF);
}
