#include "test_common.h"
#include <math.h>

TEST_F(ConcordiaTest, MathExpressions) {
    // Test @expr with math functions and arithmetic
    CompileAndLoad(
        "packet MathPacket {"
        "  @expr(sin(0.0)) float sin_zero;"
        "  @expr(cos(0.0)) float cos_zero;"
        "  @expr(pow(2.0, 3.0)) float power;"
        "  @expr(1.5 + 2.5) float add;"
        "}"
    );
    
    uint8_t buffer[16] = {0}; // 4 floats * 4 bytes
    
    // ENCODE
    // No input data needed for @expr fields as they are calculated
    clear_test_data();
    
    cnd_init(&ctx, CND_MODE_ENCODE, &program, buffer, sizeof(buffer), test_io_callback, NULL);
    EXPECT_EQ(cnd_execute(&ctx), CND_ERR_OK);
    
    // Verify output
    float val;
    
    // sin(0) = 0.0
    memcpy(&val, &buffer[0], 4);
    EXPECT_FLOAT_EQ(val, 0.0f);
    
    // cos(0) = 1.0
    memcpy(&val, &buffer[4], 4);
    EXPECT_FLOAT_EQ(val, 1.0f);
    
    // pow(2, 3) = 8.0
    memcpy(&val, &buffer[8], 4);
    EXPECT_FLOAT_EQ(val, 8.0f);
    
    // 1.5 + 2.5 = 4.0
    memcpy(&val, &buffer[12], 4);
    EXPECT_FLOAT_EQ(val, 4.0f);
}

TEST_F(ConcordiaTest, MathExpressionsWithFieldRef) {
    // Test @expr using a value from another field
    CompileAndLoad(
        "packet MathRefPacket {"
        "  uint8 x;"
        "  @expr(float(x) + 10.0) float res;"
        "}"
    );
    
    uint8_t buffer[5] = {0}; // 1 byte uint8 + 4 bytes float
    
    // ENCODE
    clear_test_data();
    g_test_data[0] = {0, 5, 0, ""}; // x = 5
    
    cnd_init(&ctx, CND_MODE_ENCODE, &program, buffer, sizeof(buffer), test_io_callback, NULL);
    EXPECT_EQ(cnd_execute(&ctx), CND_ERR_OK);
    
    // Check x
    EXPECT_EQ(buffer[0], 5);
    
    // Check res = 5.0 + 10.0 = 15.0
    float val;
    memcpy(&val, &buffer[1], 4);
    EXPECT_FLOAT_EQ(val, 15.0f);
}
