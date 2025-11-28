#include "test_common.h"

TEST_F(ConcordiaTest, SwitchEnumSugar) {
    // Test switch with Enum.Value syntax
    CompileAndLoad(
        "enum Type : uint8 { A = 10, B = 20 }"
        "packet EnumSwitch {"
        "  Type t;"
        "  switch (t) {"
        "    case Type.A: uint8 a;"
        "    case Type.B: uint8 b;"
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
