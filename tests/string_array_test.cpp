#include "test_common.h"

class StringArrayTest : public ConcordiaTest {};

TEST_F(StringArrayTest, LenAlias) {
    const char* source = R"(
        packet TestPacket {
            @len(2)
            string names[] until 0;
        }
    )";
    CompileAndLoad(source);
    // Should compile successfully
}

TEST_F(StringArrayTest, MissingPrefixOrUntil) {
    const char* source = R"(
        packet TestPacket {
            @count(2)
            string names[];
        }
    )";
    // Should fail compilation
    EXPECT_FALSE(Compile(source));
}

TEST_F(StringArrayTest, WithPrefix) {
    const char* source = R"(
        packet TestPacket {
            @count(2)
            string names[] prefix u8;
        }
    )";
    CompileAndLoad(source);
}

TEST_F(StringArrayTest, WithUntil) {
    const char* source = R"(
        packet TestPacket {
            @count(2)
            string names[] until 0;
        }
    )";
    CompileAndLoad(source);
}

TEST_F(StringArrayTest, RoundTrip) {
    const char* source = R"(
        packet TestPacket {
            @count(3)
            string names[] until 0;
        }
    )";
    CompileAndLoad(source);

    // --- Encode ---
    tctx.use_tape = true;
    tctx.tape_index = 0;
    ctx.user_ptr = &tctx;

    // Setup mock data for encoding
    // Key 0 is "names"
    g_test_data[0].key = 0; strcpy(g_test_data[0].string_val, "One");
    g_test_data[1].key = 0; strcpy(g_test_data[1].string_val, "Two");
    g_test_data[2].key = 0; strcpy(g_test_data[2].string_val, "Three");

    uint8_t buffer[100];
    memset(buffer, 0, sizeof(buffer));

    cnd_init(&ctx, CND_MODE_ENCODE, &program, buffer, sizeof(buffer), test_io_callback, &tctx);
    EXPECT_EQ(cnd_execute(&ctx), CND_ERR_OK);

    size_t encoded_size = ctx.cursor;
    // "One\0" (4) + "Two\0" (4) + "Three\0" (6) = 14 bytes
    EXPECT_EQ(encoded_size, 14);

    // --- Decode ---
    tctx.tape_index = 0; // Reset tape for verification
    // Clear mock data to ensure we are reading from buffer
    memset(g_test_data, 0, sizeof(g_test_data));
    // Reset keys to 0xFFFF so the callback knows they are free slots
    for(int i=0; i<MAX_TEST_ENTRIES; i++) g_test_data[i].key = 0xFFFF;

    cnd_vm_ctx decode_ctx;
    cnd_init(&decode_ctx, CND_MODE_DECODE, &program, buffer, encoded_size, test_io_callback, &tctx);
    EXPECT_EQ(cnd_execute(&decode_ctx), CND_ERR_OK);

    // Verify decoded data
    // The callback should have populated g_test_data sequentially
    EXPECT_EQ(g_test_data[0].key, 0); EXPECT_STREQ(g_test_data[0].string_val, "One");
    EXPECT_EQ(g_test_data[1].key, 0); EXPECT_STREQ(g_test_data[1].string_val, "Two");
    EXPECT_EQ(g_test_data[2].key, 0); EXPECT_STREQ(g_test_data[2].string_val, "Three");
}
