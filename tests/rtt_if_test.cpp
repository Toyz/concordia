#include "test_common.h"

TEST_F(ConcordiaTest, RTT_If_True) {
    CompileAndLoad(
        "packet P {"
        "  uint8 flags;"
        "  if (flags == 1) {"
        "    uint8 extra;"
        "  }"
        "}"
    );

    // 1. ENCODE (Condition True)
    // flags (key 0) = 1
    // extra (key 1) = 0xFF
    g_test_data[0].key = 0; g_test_data[0].u64_val = 1;
    g_test_data[1].key = 1; g_test_data[1].u64_val = 0xFF;

    memset(buffer, 0, sizeof(buffer));
    cnd_init(&ctx, CND_MODE_ENCODE, &program, buffer, sizeof(buffer), test_io_callback, NULL);
    ASSERT_EQ(cnd_execute(&ctx), CND_ERR_OK);

    // Verify Buffer: [0x01, 0xFF]
    EXPECT_EQ(ctx.cursor, 2);
    EXPECT_EQ(buffer[0], 0x01);
    EXPECT_EQ(buffer[1], 0xFF);

    // 2. DECODE
    clear_test_data();
    cnd_init(&ctx, CND_MODE_DECODE, &program, buffer, 2, test_io_callback, NULL);
    ASSERT_EQ(cnd_execute(&ctx), CND_ERR_OK);

    // Verify Data
    EXPECT_EQ(g_test_data[0].key, 0); EXPECT_EQ(g_test_data[0].u64_val, 1);
    EXPECT_EQ(g_test_data[1].key, 1); EXPECT_EQ(g_test_data[1].u64_val, 0xFF);
}

TEST_F(ConcordiaTest, RTT_If_False) {
    CompileAndLoad(
        "packet P {"
        "  uint8 flags;"
        "  if (flags == 1) {"
        "    uint8 extra;"
        "  }"
        "}"
    );

    // 1. ENCODE (Condition False)
    // flags (key 0) = 0
    g_test_data[0].key = 0; g_test_data[0].u64_val = 0;
    // extra should not be requested

    memset(buffer, 0, sizeof(buffer));
    cnd_init(&ctx, CND_MODE_ENCODE, &program, buffer, sizeof(buffer), test_io_callback, NULL);
    ASSERT_EQ(cnd_execute(&ctx), CND_ERR_OK);

    // Verify Buffer: [0x00]
    EXPECT_EQ(ctx.cursor, 1);
    EXPECT_EQ(buffer[0], 0x00);

    // 2. DECODE
    clear_test_data();
    cnd_init(&ctx, CND_MODE_DECODE, &program, buffer, 1, test_io_callback, NULL);
    ASSERT_EQ(cnd_execute(&ctx), CND_ERR_OK);

    // Verify Data
    EXPECT_EQ(g_test_data[0].key, 0); EXPECT_EQ(g_test_data[0].u64_val, 0);
    // Ensure no other data was read
    EXPECT_EQ(g_test_data[1].key, 0xFFFF);
}

TEST_F(ConcordiaTest, RTT_If_Else) {
    CompileAndLoad(
        "packet P {"
        "  uint8 flags;"
        "  if (flags == 1) {"
        "    uint8 a;"
        "  } else {"
        "    uint16 b;"
        "  }"
        "}"
    );

    // Case A: True -> 'a'
    g_test_data[0].key = 0; g_test_data[0].u64_val = 1;
    g_test_data[1].key = 1; g_test_data[1].u64_val = 0xAA; // 'a'

    memset(buffer, 0, sizeof(buffer));
    cnd_init(&ctx, CND_MODE_ENCODE, &program, buffer, sizeof(buffer), test_io_callback, NULL);
    ASSERT_EQ(cnd_execute(&ctx), CND_ERR_OK);

    EXPECT_EQ(ctx.cursor, 2);
    EXPECT_EQ(buffer[0], 1);
    EXPECT_EQ(buffer[1], 0xAA);

    // Case B: False -> 'b'
    clear_test_data();
    g_test_data[0].key = 0; g_test_data[0].u64_val = 2; // Not 1
    g_test_data[1].key = 2; g_test_data[1].u64_val = 0xBBCC; // 'b' (key 2)

    memset(buffer, 0, sizeof(buffer));
    cnd_init(&ctx, CND_MODE_ENCODE, &program, buffer, sizeof(buffer), test_io_callback, NULL);
    ASSERT_EQ(cnd_execute(&ctx), CND_ERR_OK);

    EXPECT_EQ(ctx.cursor, 3);
    EXPECT_EQ(buffer[0], 2);
    // Little endian uint16
    EXPECT_EQ(buffer[1], 0xCC);
    EXPECT_EQ(buffer[2], 0xBB);
}

TEST_F(ConcordiaTest, RTT_Nested_If) {
    CompileAndLoad(
        "packet P {"
        "  uint8 x;"
        "  uint8 y;"
        "  if (x > 10) {"
        "    if (y < 5) {"
        "      uint8 z;"
        "    }"
        "  }"
        "}"
    );

    // Case: x=20, y=2 -> z included
    g_test_data[0].key = 0; g_test_data[0].u64_val = 20;
    g_test_data[1].key = 1; g_test_data[1].u64_val = 2;
    g_test_data[2].key = 2; g_test_data[2].u64_val = 0xFF;

    memset(buffer, 0, sizeof(buffer));
    cnd_init(&ctx, CND_MODE_ENCODE, &program, buffer, sizeof(buffer), test_io_callback, NULL);
    ASSERT_EQ(cnd_execute(&ctx), CND_ERR_OK);
    EXPECT_EQ(ctx.cursor, 3);

    // Case: x=20, y=10 -> z excluded
    clear_test_data();
    g_test_data[0].key = 0; g_test_data[0].u64_val = 20;
    g_test_data[1].key = 1; g_test_data[1].u64_val = 10;

    memset(buffer, 0, sizeof(buffer));
    cnd_init(&ctx, CND_MODE_ENCODE, &program, buffer, sizeof(buffer), test_io_callback, NULL);
    ASSERT_EQ(cnd_execute(&ctx), CND_ERR_OK);
    EXPECT_EQ(ctx.cursor, 2);
}
