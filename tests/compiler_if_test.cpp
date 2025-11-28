#include "test_common.h"
#include "../src/compiler/cnd_internal.h"

TEST(CompilerIfTest, IfStatement) {
    const char* source = 
        "packet MyPacket {\n"
        "    uint8 flags;\n"
        "    if (flags & 1) {\n"
        "        uint8 extra;\n"
        "    }\n"
        "}";
    
    FILE* f = fopen("test_if.cnd", "wb");
    fwrite(source, 1, strlen(source), f);
    fclose(f);
    
    int result = cnd_compile_file("test_if.cnd", "test_if.cndil", 0);
    EXPECT_EQ(result, 0);
    
    // Read back the bytecode
    FILE* out = fopen("test_if.cndil", "rb");
    ASSERT_TRUE(out != NULL);
    fseek(out, 0, SEEK_END);
    long size = ftell(out);
    fseek(out, 0, SEEK_SET);
    uint8_t* data = (uint8_t*)malloc(size);
    fread(data, 1, size, out);
    fclose(out);
    
    // Basic validation of header
    EXPECT_EQ(memcmp(data, "CNDIL", 5), 0);
    
    uint32_t bc_offset;
    memcpy(&bc_offset, data + 12, 4);
    
    bool found_jump_false = false;
    bool found_jump = false;
    bool found_bit_and = false;
    
    for (long i = bc_offset; i < size; i++) {
        if (data[i] == OP_JUMP_IF_NOT) found_jump_false = true;
        if (data[i] == OP_JUMP) found_jump = true;
        if (data[i] == OP_BIT_AND) found_bit_and = true;
    }
    
    EXPECT_TRUE(found_jump_false);
    EXPECT_TRUE(found_jump);
    EXPECT_TRUE(found_bit_and);
    
    free(data);
    remove("test_if.cnd");
    remove("test_if.cndil");
}

TEST(CompilerIfTest, IfElseStatement) {
    const char* source = 
        "packet MyPacket {\n"
        "    uint8 flags;\n"
        "    if (flags == 0) {\n"
        "        uint8 a;\n"
        "    } else {\n"
        "        uint16 b;\n"
        "    }\n"
        "}";
        
    FILE* f = fopen("test_if_else.cnd", "wb");
    fwrite(source, 1, strlen(source), f);
    fclose(f);
    
    int result = cnd_compile_file("test_if_else.cnd", "test_if_else.cndil", 0);
    EXPECT_EQ(result, 0);
    
    remove("test_if_else.cnd");
    remove("test_if_else.cndil");
}

TEST(CompilerIfTest, NestedIf) {
    const char* source = 
        "packet MyPacket {\n"
        "    uint8 a;\n"
        "    uint8 b;\n"
        "    if (a > 10) {\n"
        "        if (b < 5) {\n"
        "             uint8 c;\n"
        "        }\n"
        "    }\n"
        "}";
        
    FILE* f = fopen("test_nested_if.cnd", "wb");
    fwrite(source, 1, strlen(source), f);
    fclose(f);
    
    int result = cnd_compile_file("test_nested_if.cnd", "test_nested_if.cndil", 0);
    EXPECT_EQ(result, 0);
    
    remove("test_nested_if.cnd");
    remove("test_nested_if.cndil");
}

TEST(CompilerIfTest, ComplexExpression) {
    const char* source = 
        "packet MyPacket {\n"
        "    uint8 a;\n"
        "    uint8 b;\n"
        "    if ((a & 0xF) == 1 && (b | 2) > 5) {\n"
        "        uint8 c;\n"
        "    }\n"
        "}";
        
    FILE* f = fopen("test_complex_expr.cnd", "wb");
    fwrite(source, 1, strlen(source), f);
    fclose(f);
    
    int result = cnd_compile_file("test_complex_expr.cnd", "test_complex_expr.cndil", 0);
    EXPECT_EQ(result, 0);
    
    remove("test_complex_expr.cnd");
    remove("test_complex_expr.cndil");
}
