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

TEST_F(ConcordiaTest, SwitchInsideStruct) {
    CompileAndLoad(
        "struct Container {"
        "  uint8 t;"
        "  switch(t) {"
        "    case 1: uint8 v1;"
        "    case 2: uint16 v2;"
        "  }"
        "}"
        "packet P { Container c; }"
    );
    
    // Case 2 (v2)
    // Inside struct `c`, keys are local to struct definition order?
    // `t` is Key 0. `v1` is Key 1. `v2` is Key 2.
    // Struct `c` is Key 0 in Packet P.
    // When entering struct `c`, VM pushes scope? No, Keys are flattened in global table by Compiler if inlined?
    // Or `OP_ENTER_STRUCT` maintains hierarchy?
    // `parse_struct` creates a `StructDef` with its own `StringTable`?
    // No, `Parser` has one global `StringTable`.
    // `parse_struct` uses the global `strtab` to register keys.
    // So KeyIDs are sequential across the whole file.
    // Order:
    // Container.t -> Key 0
    // Container.v1 -> Key 1
    // Container.v2 -> Key 2
    // P.c -> Key 3
    
    // Let's verify Key IDs by `cnd inspect` mentally:
    // 0: t, 1: v1, 2: v2, 3: c.
    
    // Data:
    // 1. c (Key 3) -> ENTER_STRUCT
    // 2. t (Key 0) -> 2
    // 3. v2 (Key 2) -> 0x1234
    
    clear_test_data();
    // g_test_data indices match keys if keys are 0,1,2...
    // But here execution order:
    // 1. P.c (Key 3)
    // 2. Container.t (Key 0)
    // 3. Switch -> Case 2 -> Container.v2 (Key 2)
    
    // `test_io_callback` uses search or tape.
    // We used search in previous tests.
    // So we populate by Key.
    
    g_test_data[0] = {0, 2, 0, ""}; // Key 0 (t) = 2
    g_test_data[1] = {2, 0x3412, 0, ""}; // Key 2 (v2) = 0x3412 (LE)
    
    uint8_t buffer[8] = {0};
    cnd_init(&ctx, CND_MODE_ENCODE, &program, buffer, sizeof(buffer), test_io_callback, NULL);
    EXPECT_EQ(cnd_execute(&ctx), CND_ERR_OK);
    
    // buffer[0] = t = 2
    // buffer[1] = 0x12
    // buffer[2] = 0x34
    EXPECT_EQ(buffer[0], 2);
    EXPECT_EQ(buffer[1], 0x12);
    EXPECT_EQ(buffer[2], 0x34);
}
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

TEST_F(EnumTest, EnumEndianness) {
    CompileAndLoad(
        "enum E : uint16 { Val = 0x1234 }"
        "packet P {"
        "  @big_endian E be;"
        "  @little_endian E le;"
        "}"
    );

    g_test_data[0].key = 0; g_test_data[0].u64_val = 0x1234;
    g_test_data[1].key = 1; g_test_data[1].u64_val = 0x1234;

    memset(buffer, 0, sizeof(buffer));
    cnd_init(&ctx, CND_MODE_ENCODE, &program, buffer, sizeof(buffer), test_io_callback, &tctx);
    cnd_error_t err = cnd_execute(&ctx);
    EXPECT_EQ(err, CND_ERR_OK);

    // Big Endian: 0x12 0x34
    EXPECT_EQ(buffer[0], 0x12);
    EXPECT_EQ(buffer[1], 0x34);

    // Little Endian: 0x34 0x12
    EXPECT_EQ(buffer[2], 0x34);
    EXPECT_EQ(buffer[3], 0x12);
}
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
    // 'magic' is validated and now returned to callback (Read-Only Const).
    EXPECT_EQ(g_test_data[0].key, 0);
    EXPECT_EQ(g_test_data[0].u64_val, 0x42);
    EXPECT_EQ(g_test_data[1].key, 1);
    EXPECT_EQ(g_test_data[1].u64_val, 0xFF);

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
