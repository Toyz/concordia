#include "test_common.h"

class EnumTest : public ConcordiaTest {};

TEST_F(EnumTest, BasicEnum) {
    CompileAndLoad(
        "enum Color : uint8 {"
        "  Red = 1,"
        "  Green = 2,"
        "  Blue = 3"
        "}"
        "packet P {"
        "  Color c;"
        "}"
    );
    
    g_test_data[0].key = 0; g_test_data[0].u64_val = 2; // Green
    
    memset(buffer, 0, sizeof(buffer));
    cnd_init(&ctx, CND_MODE_ENCODE, &program, buffer, sizeof(buffer), test_io_callback, &tctx);
    cnd_error_t err = cnd_execute(&ctx);
    EXPECT_EQ(err, CND_ERR_OK);
    
    EXPECT_EQ(buffer[0], 2);
}

TEST_F(EnumTest, EnumDefaultType) {
    CompileAndLoad(
        "enum Status {"
        "  Ok = 0,"
        "  Error = 1"
        "}"
        "packet P {"
        "  Status s;"
        "}"
    );
    
    // Default is uint32 (4 bytes)
    g_test_data[0].key = 0; g_test_data[0].u64_val = 1;
    
    memset(buffer, 0, sizeof(buffer));
    cnd_init(&ctx, CND_MODE_ENCODE, &program, buffer, sizeof(buffer), test_io_callback, &tctx);
    cnd_error_t err = cnd_execute(&ctx);
    EXPECT_EQ(err, CND_ERR_OK);
    
    EXPECT_EQ(buffer[0], 1);
    EXPECT_EQ(buffer[1], 0);
    EXPECT_EQ(buffer[2], 0);
    EXPECT_EQ(buffer[3], 0);
}

TEST_F(EnumTest, EnumWithRange) {
    CompileAndLoad(
        "enum Level : uint8 {"
        "  Low = 10,"
        "  High = 20"
        "}"
        "packet P {"
        "  @range(10, 20) Level l;"
        "}"
    );
    
    // 15 is in range (10-20) but NOT in the enum list.
    // Should fail with CND_ERR_VALIDATION due to strict enum check.
    g_test_data[0].key = 0; g_test_data[0].u64_val = 15; 
    
    memset(buffer, 0, sizeof(buffer));
    cnd_init(&ctx, CND_MODE_ENCODE, &program, buffer, sizeof(buffer), test_io_callback, &tctx);
    cnd_error_t err = cnd_execute(&ctx);
    EXPECT_EQ(err, CND_ERR_VALIDATION);

    // Now try a valid value
    g_test_data[0].u64_val = 10;
    cnd_init(&ctx, CND_MODE_ENCODE, &program, buffer, sizeof(buffer), test_io_callback, &tctx);
    err = cnd_execute(&ctx);
    EXPECT_EQ(err, CND_ERR_OK);
    EXPECT_EQ(buffer[0], 10);
}

TEST_F(EnumTest, EnumImport) {
    // Create a temporary file for import
    FILE* f = fopen("enum_def.cnd", "wb");
    fprintf(f, "enum SharedEnum : uint16 { A = 100, B = 200 }");
    fclose(f);
    
    CompileAndLoad(
        "@import(\"enum_def.cnd\")"
        "packet P {"
        "  SharedEnum e;"
        "}"
    );
    
    g_test_data[0].key = 0; g_test_data[0].u64_val = 200;
    
    memset(buffer, 0, sizeof(buffer));
    cnd_init(&ctx, CND_MODE_ENCODE, &program, buffer, sizeof(buffer), test_io_callback, &tctx);
    cnd_error_t err = cnd_execute(&ctx);
    EXPECT_EQ(err, CND_ERR_OK);
    
    EXPECT_EQ(buffer[0], 200);
    EXPECT_EQ(buffer[1], 0);
    
    remove("enum_def.cnd");
}
