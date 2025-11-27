#include "test_common.h"

TEST_F(ConcordiaTest, MatchRTT_U8) {
    CompileAndLoad(
        "packet P {"
        "  @match(0x42) uint8 magic;"
        "  uint8 data;"
        "}"
    );

    // 1. ENCODE
    // We only provide 'data'. 'magic' should be auto-filled.
    g_test_data[0].key = 1; // 'data' (key 0 is magic, key 1 is data)
    g_test_data[0].u64_val = 0xFF;
    
    // Note: In our parser, keys are assigned sequentially.
    // magic -> key 0
    // data -> key 1
    // But wait, OP_CONST_CHECK does NOT take a key argument in the IL!
    // Let's check the parser again.
    // buf_push(p->target, OP_CONST_CHECK); buf_push(p->target, type_op); ...
    // It does NOT emit a key ID for the const field itself.
    // However, the field definition usually emits a key ID.
    // In cnd_parser.c:
    // if (has_const) { ... emit OP_CONST_CHECK ... } else { ... emit OP_IO_... with key ... }
    // So for a const/match field, NO IO opcode is emitted, and thus NO key is associated with it in the IL for IO purposes.
    // The next field 'data' will have key 1 (because key 0 was assigned to 'magic' in the string table).
    
    g_test_data[0].key = 1; // 'data'
    g_test_data[0].u64_val = 0xFF;

    memset(buffer, 0, sizeof(buffer));
    cnd_init(&ctx, CND_MODE_ENCODE, &program, buffer, sizeof(buffer), test_io_callback, NULL);
    cnd_error_t err = cnd_execute(&ctx);
    ASSERT_EQ(err, CND_ERR_OK);

    // Verify Buffer
    EXPECT_EQ(buffer[0], 0x42); // Magic
    EXPECT_EQ(buffer[1], 0xFF); // Data
    EXPECT_EQ(ctx.cursor, 2);

    // 2. DECODE (Success)
    // We use the same buffer.
    // We need to clear test data to verify we read 'data' back.
    clear_test_data();
    
    cnd_init(&ctx, CND_MODE_DECODE, &program, buffer, 2, test_io_callback, NULL);
    err = cnd_execute(&ctx);
    ASSERT_EQ(err, CND_ERR_OK);
    
    // Verify we got 'data' back
    // 'magic' is validated silently, not returned to callback because there is no IO op for it.
    EXPECT_EQ(g_test_data[0].key, 1);
    EXPECT_EQ(g_test_data[0].u64_val, 0xFF);

    // 3. DECODE (Failure)
    buffer[0] = 0x43; // Wrong magic
    cnd_init(&ctx, CND_MODE_DECODE, &program, buffer, 2, test_io_callback, NULL);
    err = cnd_execute(&ctx);
    EXPECT_EQ(err, CND_ERR_VALIDATION);
}

TEST_F(ConcordiaTest, MatchRTT_U32_BigEndian) {
    CompileAndLoad(
        "packet P {"
        "  @big_endian @match(0xDEADBEEF) uint32 magic;"
        "}"
    );

    // ENCODE
    memset(buffer, 0, sizeof(buffer));
    cnd_init(&ctx, CND_MODE_ENCODE, &program, buffer, sizeof(buffer), test_io_callback, NULL);
    cnd_error_t err = cnd_execute(&ctx);
    ASSERT_EQ(err, CND_ERR_OK);

    // Verify Buffer (Big Endian)
    EXPECT_EQ(buffer[0], 0xDE);
    EXPECT_EQ(buffer[1], 0xAD);
    EXPECT_EQ(buffer[2], 0xBE);
    EXPECT_EQ(buffer[3], 0xEF);

    // DECODE (Success)
    cnd_init(&ctx, CND_MODE_DECODE, &program, buffer, 4, test_io_callback, NULL);
    err = cnd_execute(&ctx);
    ASSERT_EQ(err, CND_ERR_OK);

    // DECODE (Failure)
    buffer[3] = 0xEE;
    cnd_init(&ctx, CND_MODE_DECODE, &program, buffer, 4, test_io_callback, NULL);
    err = cnd_execute(&ctx);
    EXPECT_EQ(err, CND_ERR_VALIDATION);
}
