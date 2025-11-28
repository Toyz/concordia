#include "test_common.h"

TEST_F(ConcordiaTest, BooleanType) {
    // Test standard boolean (byte-aligned)
    // bool x; -> OP_IO_BOOL
    CompileAndLoad("packet Bools { bool flag_true; bool flag_false; }");
    
    uint8_t buffer[2] = {0};
    
    // ENCODE
    // Host provides true (1) and false (0)
    test_data_entry entries[] = {
        {0, 1, 0, ""}, // flag_true (key 0) -> 1
        {1, 0, 0, ""}  // flag_false (key 1) -> 0
    };
    // We need to inject this data into the test_common mock
    clear_test_data();
    g_test_data[0] = entries[0];
    g_test_data[1] = entries[1];
    
    cnd_init(&ctx, CND_MODE_ENCODE, &program, buffer, sizeof(buffer), test_io_callback, NULL);
    EXPECT_EQ(cnd_execute(&ctx), CND_ERR_OK);
    
    EXPECT_EQ(buffer[0], 1);
    EXPECT_EQ(buffer[1], 0);
    
    // DECODE
    cnd_init(&ctx, CND_MODE_DECODE, &program, buffer, sizeof(buffer), test_io_callback, NULL);
    EXPECT_EQ(cnd_execute(&ctx), CND_ERR_OK);
    
    // VALIDATION ERROR check
    // If buffer has 2 (invalid boolean), decoding should fail
    buffer[0] = 2;
    cnd_init(&ctx, CND_MODE_DECODE, &program, buffer, sizeof(buffer), test_io_callback, NULL);
    EXPECT_EQ(cnd_execute(&ctx), CND_ERR_VALIDATION);
}

TEST_F(ConcordiaTest, BooleanBitfield) {
    // Test bitfield boolean
    // bool a : 1; bool b : 1;
    CompileAndLoad("packet BitBools { bool a : 1; bool b : 1; }");
    
    uint8_t buffer[1] = {0};
    
    // ENCODE
    clear_test_data();
    g_test_data[0] = {0, 1, 0, ""}; // a = 1
    g_test_data[1] = {1, 0, 0, ""}; // b = 0
    
    cnd_init(&ctx, CND_MODE_ENCODE, &program, buffer, sizeof(buffer), test_io_callback, NULL);
    EXPECT_EQ(cnd_execute(&ctx), CND_ERR_OK);
    
    // Byte should be:
    // Bit 0: 1 (a)
    // Bit 1: 0 (b)
    // Bits 2-7: 0 (pad)
    // Result: 00000001 = 0x01
    EXPECT_EQ(buffer[0], 1);
    
    // ENCODE (Both True)
    clear_test_data();
    g_test_data[0] = {0, 1, 0, ""}; // a = 1
    g_test_data[1] = {1, 1, 0, ""}; // b = 1
    
    buffer[0] = 0;
    cnd_init(&ctx, CND_MODE_ENCODE, &program, buffer, sizeof(buffer), test_io_callback, NULL);
    EXPECT_EQ(cnd_execute(&ctx), CND_ERR_OK);
    
    // Bit 0: 1
    // Bit 1: 1
    // Result: 00000011 = 0x03
    EXPECT_EQ(buffer[0], 3);
}

TEST_F(ConcordiaTest, BooleanBitfieldValidation) {
    CompileAndLoad("packet Val { bool a : 1; }");
    
    uint8_t buffer[1] = {0};
    
    // ENCODE with invalid value (2)
    clear_test_data();
    g_test_data[0] = {0, 2, 0, ""}; 
    
    cnd_init(&ctx, CND_MODE_ENCODE, &program, buffer, sizeof(buffer), test_io_callback, NULL);
    EXPECT_EQ(cnd_execute(&ctx), CND_ERR_VALIDATION);
}
