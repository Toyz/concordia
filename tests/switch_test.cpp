#include "test_common.h"

TEST_F(ConcordiaTest, SwitchBasic) {
    // Test basic switch with integer constants
    CompileAndLoad(
        "packet SwitchPacket {"
        "  uint8 type;"
        "  switch (type) {"
        "    case 1: uint8 val_a;"
        "    case 2: uint16 val_b;"
        "    default: uint32 val_def;"
        "  }"
        "}"
    );
    
    // Case 1
    clear_test_data();
    g_test_data[0] = {0, 1, 0, ""}; // type = 1
    g_test_data[1] = {1, 0xAA, 0, ""}; // val_a = 0xAA
    
    uint8_t buffer[8] = {0};
    cnd_init(&ctx, CND_MODE_ENCODE, &program, buffer, sizeof(buffer), test_io_callback, NULL);
    EXPECT_EQ(cnd_execute(&ctx), CND_ERR_OK);
    
    EXPECT_EQ(buffer[0], 1);
    EXPECT_EQ(buffer[1], 0xAA);
    // Should not have written val_b or val_def
    EXPECT_EQ(buffer[2], 0);
    
    // Case 2
    clear_test_data();
    g_test_data[0] = {0, 2, 0, ""}; // type = 2
    g_test_data[1] = {2, 0xBBCC, 0, ""}; // val_b = 0xBBCC
    
    memset(buffer, 0, sizeof(buffer));
    cnd_init(&ctx, CND_MODE_ENCODE, &program, buffer, sizeof(buffer), test_io_callback, NULL);
    EXPECT_EQ(cnd_execute(&ctx), CND_ERR_OK);
    
    EXPECT_EQ(buffer[0], 2);
    EXPECT_EQ(buffer[1], 0xCC);
    EXPECT_EQ(buffer[2], 0xBB);
    
    // Default
    clear_test_data();
    g_test_data[0] = {0, 99, 0, ""}; // type = 99
    g_test_data[1] = {3, 0xDEADBEEF, 0, ""}; // val_def
    
    memset(buffer, 0, sizeof(buffer));
    cnd_init(&ctx, CND_MODE_ENCODE, &program, buffer, sizeof(buffer), test_io_callback, NULL);
    EXPECT_EQ(cnd_execute(&ctx), CND_ERR_OK);
    
    EXPECT_EQ(buffer[0], 99);
    EXPECT_EQ(buffer[1], 0xEF);
    EXPECT_EQ(buffer[2], 0xBE);
    EXPECT_EQ(buffer[3], 0xAD);
    EXPECT_EQ(buffer[4], 0xDE);
}

TEST_F(ConcordiaTest, SwitchEnum) {
    // Test switch with Enum Values (currently as integers, later as symbols)
    CompileAndLoad(
        "enum Type : uint8 { A = 10, B = 20 }"
        "packet EnumSwitch {"
        "  Type t;"
        "  switch (t) {"
        "    case 10: uint8 a;"
        "    case 20: uint8 b;"
        "  }"
        "}"
    );
    
    // Case A (10)
    clear_test_data();
    g_test_data[0] = {0, 10, 0, ""}; // t = 10
    g_test_data[1] = {1, 0x11, 0, ""}; // a
    
    uint8_t buffer[4] = {0};
    cnd_init(&ctx, CND_MODE_ENCODE, &program, buffer, sizeof(buffer), test_io_callback, NULL);
    EXPECT_EQ(cnd_execute(&ctx), CND_ERR_OK);
    EXPECT_EQ(buffer[0], 10);
    EXPECT_EQ(buffer[1], 0x11);
}

TEST_F(ConcordiaTest, SwitchNoDefault) {
    // Switch without default. Should skip if no match.
    CompileAndLoad(
        "packet NoDef {"
        "  uint8 t;"
        "  switch (t) {"
        "    case 1: uint8 val;"
        "  }"
        "  uint8 end;"
        "}"
    );
    
    // No Match (t=2)
    clear_test_data();
    g_test_data[0] = {0, 2, 0, ""}; // t = 2
    g_test_data[1] = {2, 0xFF, 0, ""}; // end = 0xFF (Key 2 because val is Key 1)
    
    uint8_t buffer[4] = {0};
    cnd_init(&ctx, CND_MODE_ENCODE, &program, buffer, sizeof(buffer), test_io_callback, NULL);
    EXPECT_EQ(cnd_execute(&ctx), CND_ERR_OK);
    
    EXPECT_EQ(buffer[0], 2);
    // Skipped 'val' (index 1), went straight to 'end' (index 2)
    // So buffer[1] should be 'end'.
    EXPECT_EQ(buffer[1], 0xFF);
}

TEST_F(ConcordiaTest, SwitchImportedEnum) {
    const char* kSharedFile = "shared.cnd";
    std::ofstream out(kSharedFile);
    out << "enum SharedEnum : uint8 { VAL_ONE = 1, VAL_TWO = 2 }";
    out.close();

    CompileAndLoad(
        "@import(\"shared.cnd\")"
        "packet P {"
        "  SharedEnum t;"
        "  switch (t) {"
        "    case SharedEnum.VAL_ONE: uint8 a;"
        "    case SharedEnum.VAL_TWO: uint16 b;"
        "  }"
        "}"
    );

    // Case VAL_TWO (2)
    clear_test_data();
    g_test_data[0] = {0, 2, 0, ""}; // t = 2
    g_test_data[1] = {2, 0xABCD, 0, ""}; // b (Key 2)

    uint8_t buffer[8] = {0};
    cnd_init(&ctx, CND_MODE_ENCODE, &program, buffer, sizeof(buffer), test_io_callback, NULL);
    EXPECT_EQ(cnd_execute(&ctx), CND_ERR_OK);
    
    EXPECT_EQ(buffer[0], 2);
    EXPECT_EQ(buffer[1], 0xCD);
    EXPECT_EQ(buffer[2], 0xAB);

    remove(kSharedFile);
}
