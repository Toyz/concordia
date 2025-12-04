#include "test_common.h"

TEST_F(ConcordiaTest, ArrayEOF_Decode) {
    // Schema: @eof uint8 data[];
    // IL: OP_ARR_EOF, Key(1), OP_IO_U8, Key(2), OP_ARR_END
    uint8_t il[] = { 
        OP_ARR_EOF, 0x01, 0x00, 
        OP_IO_U8, 0x02, 0x00, 
        OP_ARR_END 
    };
    
    // Input Data: 3 bytes
    uint8_t input[] = { 0xAA, 0xBB, 0xCC };
    
    cnd_program_load(&program, il, sizeof(il));
    cnd_init(&ctx, CND_MODE_DECODE, &program, input, sizeof(input), test_io_callback, NULL);
    
    // We expect the callback to be called 3 times for Key 2 (data element)
    // and once for Key 1 (array start) and once for Key 0 (array end)
    
    // Reset global test data
    clear_test_data();
    
    cnd_error_t err = cnd_execute(&ctx);
    EXPECT_EQ(err, CND_ERR_OK);
    
    // Verify we read all data
    EXPECT_EQ(ctx.cursor, 3);
}

TEST_F(ConcordiaTest, ArrayDynamic_Decode) {
    // Schema: uint8 len; @count(len) uint8 data[];
    // IL: OP_IO_U8, Key(1), OP_ARR_DYNAMIC, Key(2), RefKey(1), OP_IO_U8, Key(3), OP_ARR_END
    uint8_t il[] = { 
        OP_IO_U8, 0x01, 0x00,
        OP_ARR_DYNAMIC, 0x02, 0x00, 0x01, 0x00,
        OP_IO_U8, 0x03, 0x00,
        OP_ARR_END
    };
    
    // Input Data: len=3, data=[0x10, 0x20, 0x30]
    uint8_t input[] = { 0x03, 0x10, 0x20, 0x30 };
    
    cnd_program_load(&program, il, sizeof(il));
    cnd_init(&ctx, CND_MODE_DECODE, &program, input, sizeof(input), test_io_callback, NULL);
    
    // The callback needs to store the value of Key 1 so it can be retrieved by OP_ARR_DYNAMIC
    // test_io_callback in test_common.cpp usually just stores/retrieves from g_test_data
    // We need to ensure it handles OP_CTX_QUERY correctly if that's what OP_ARR_DYNAMIC uses.
    // Looking at vm_exec.c: OP_ARR_DYNAMIC calls callback with OP_CTX_QUERY on ref_key.
    
    // Let's verify test_io_callback handles OP_CTX_QUERY.
    // It usually doesn't store state between calls unless we set it up.
    // But for Decode, OP_IO_U8 will call callback with value read.
    // We need that value to be available for the subsequent OP_CTX_QUERY.
    
    // In test_common.cpp, test_io_callback usually just logs or returns static data.
    // We might need a smarter callback or pre-populate g_test_data.
    
    // Pre-populate Key 1 with 3.
    // But wait, in Decode mode, OP_IO_U8 reads from buffer (0x03) and calls callback.
    // The callback should update g_test_data[key].
    // Let's check test_common.cpp.
    
    cnd_error_t err = cnd_execute(&ctx);
    EXPECT_EQ(err, CND_ERR_OK);
    EXPECT_EQ(ctx.cursor, 4);
}
