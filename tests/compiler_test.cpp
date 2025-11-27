#include <gtest/gtest.h>
#include <fstream>
#include <cstdio>
#include "compiler.h"

class CompilerTest : public ::testing::Test {
protected:
    const char* kSourceFile = "test_temp.cnd";
    const char* kOutFile = "test_temp.il";

    void TearDown() override {
        remove(kSourceFile);
        remove(kOutFile);
    }

    void WriteSource(const std::string& content) {
        std::ofstream out(kSourceFile);
        out << content;
        out.close();
    }

    // Returns true if file exists and size > 0
    bool CheckOutputExists() {
        std::ifstream f(kOutFile, std::ios::binary | std::ios::ate);
        return f.good() && f.tellg() > 0;
    }
};

TEST_F(CompilerTest, BasicStruct) {
    WriteSource("struct Point { float x; float y; }");
    int res = cnd_compile_file(kSourceFile, kOutFile, 0);
    EXPECT_EQ(res, 0);
    EXPECT_TRUE(CheckOutputExists());
}

TEST_F(CompilerTest, AllPrimitives) {
    WriteSource(
        "struct AllTypes {"
        "  uint8 u8; uint16 u16; uint32 u32; uint64 u64;"
        "  int8 i8; int16 i16; int32 i32; int64 i64;"
        "  float f32; double f64;"
        "}"
    );
    int res = cnd_compile_file(kSourceFile, kOutFile, 0);
    EXPECT_EQ(res, 0);
    EXPECT_TRUE(CheckOutputExists());
}

TEST_F(CompilerTest, ArraysAndStrings) {
    WriteSource(
        "struct Arrays {"
        "  uint8 fixed[4];"
        "  uint16 var[] prefix uint8;"
        "  string s1;"
        "  string s2 prefix uint16;"
        "}"
    );
    int res = cnd_compile_file(kSourceFile, kOutFile, 0);
    EXPECT_EQ(res, 0);
    EXPECT_TRUE(CheckOutputExists());
}

TEST_F(CompilerTest, Decorators) {
    WriteSource(
        "struct Decorated {"
        "  @range(0, 100) uint8 score;"
        "  @const(0xCAFE) uint16 magic;"
        "  @big_endian uint32 be_val;"
        "}"
    );
    int res = cnd_compile_file(kSourceFile, kOutFile, 0);
    EXPECT_EQ(res, 0);
    EXPECT_TRUE(CheckOutputExists());
}

TEST_F(CompilerTest, InvalidSyntax) {
    // Missing closing brace
    WriteSource("struct Broken { uint8 x;"); 
    
    // Redirect stdout to suppress error printing during test
    testing::internal::CaptureStdout();
    int res = cnd_compile_file(kSourceFile, kOutFile, 0);
    testing::internal::GetCapturedStdout();
    
    EXPECT_NE(res, 0);
}

TEST_F(CompilerTest, UnknownType) {
    WriteSource("struct BadType { mystery_type x; };");
    
    testing::internal::CaptureStdout();
    int res = cnd_compile_file(kSourceFile, kOutFile, 0);
    testing::internal::GetCapturedStdout();
    
    EXPECT_NE(res, 0);
}

TEST_F(CompilerTest, NestedStructs) {
    WriteSource(
        "struct Inner { uint8 val; }"
        "struct Outer { Inner i; }"
    );
    int res = cnd_compile_file(kSourceFile, kOutFile, 0);
    EXPECT_EQ(res, 0);
    EXPECT_TRUE(CheckOutputExists());
}

TEST_F(CompilerTest, PacketDefinition) {
    WriteSource(
        "packet Telemetry {"
        "  uint16 id;"
        "  uint32 timestamp;"
        "}"
    );
    int res = cnd_compile_file(kSourceFile, kOutFile, 0);
    EXPECT_EQ(res, 0);
    EXPECT_TRUE(CheckOutputExists());
}

/*
TEST_F(CompilerTest, DependsOn) {
    WriteSource(
        "struct Payload { uint8 data; };"
        "packet Message {"
        "  uint8 version;"
        "  @depends_on(version) uint16 extra_field;"
        "  @depends_on(extra_field) Payload optional_payload;"
        "}"
    );
    int res = cnd_compile_file(kSourceFile, kOutFile, 0);
    EXPECT_EQ(res, 0);
    EXPECT_TRUE(CheckOutputExists());
}
*/

TEST_F(CompilerTest, BitfieldSyntax) {
    WriteSource(
        "struct Bitfields {"
        "  uint8 f1 : 1;"
        "  uint8 f2 : 3;"
        "  uint16 f3 : 12;"
        "}"
    );
    int res = cnd_compile_file(kSourceFile, kOutFile, 0);
    EXPECT_EQ(res, 0);
    EXPECT_TRUE(CheckOutputExists());
}

TEST_F(CompilerTest, CRC32Syntax) {
    WriteSource(
        "packet Checksum {"
        "  uint8 data[10];"
        "  @crc(32) uint32 crc;"
        "}"
    );
    int res = cnd_compile_file(kSourceFile, kOutFile, 0);
    EXPECT_EQ(res, 0);
    EXPECT_TRUE(CheckOutputExists());
}

TEST_F(CompilerTest, PaddingAndFill) {
    WriteSource(
        "struct Layout {"
        "  uint8 a : 4;"
        "  @pad(4) uint8 dummy;"
        "  @fill uint8 aligned;"
        "}"
    );
    int res = cnd_compile_file(kSourceFile, kOutFile, 0);
    EXPECT_EQ(res, 0);
    EXPECT_TRUE(CheckOutputExists());
}

TEST_F(CompilerTest, Transformations) {
    WriteSource(
        "struct Transforms {"
        "  @mul(10) @add(5) uint8 val1;"
        "  @div(2) @sub(1) uint16 val2;"
        "  @scale(0.5) @offset(100.0) float val3;"
        "}"
    );
    int res = cnd_compile_file(kSourceFile, kOutFile, 0);
    EXPECT_EQ(res, 0);
    EXPECT_TRUE(CheckOutputExists());
}

TEST_F(CompilerTest, EmptyStruct) {
    WriteSource("struct Empty {}");
    int res = cnd_compile_file(kSourceFile, kOutFile, 0);
    EXPECT_EQ(res, 0);
    EXPECT_TRUE(CheckOutputExists());
}

TEST_F(CompilerTest, InvalidDecorator) {
    WriteSource("struct BadDec { @nonexistent(1) uint8 x; }");
    
    testing::internal::CaptureStdout();
    int res = cnd_compile_file(kSourceFile, kOutFile, 0);
    testing::internal::GetCapturedStdout();
    
    EXPECT_NE(res, 0);
}

TEST_F(CompilerTest, ShorthandTypes) {
    WriteSource(
        "struct Shorthands {"
        "  u8 a; u16 b; u32 c; u64 d;"
        "  i8 e; i16 f; i32 g; i64 h;"
        "  @const(1) u8 i;"
        "  @const(2) u16 j;"
        "  @const(3) u32 k;"
        "  @const(4) u64 l;"
        "}"
    );
    int res = cnd_compile_file(kSourceFile, kOutFile, 0);
    EXPECT_EQ(res, 0);
    EXPECT_TRUE(CheckOutputExists());
}

TEST_F(CompilerTest, ParameterizedFill) {
    WriteSource(
        "struct FillParams {"
        "  u8 a : 1;"
        "  @fill(1) u8 b;"
        "  u8 c : 1;"
        "  @fill(0) u8 d;"
        "  u8 e : 1;"
        "  @fill u8 f;"
        "}"
    );
    int res = cnd_compile_file(kSourceFile, kOutFile, 0);
    EXPECT_EQ(res, 0);
    EXPECT_TRUE(CheckOutputExists());
}

TEST_F(CompilerTest, InvalidFillParam) {
    WriteSource("struct BadFill { @fill(2) u8 x; }");
    
    testing::internal::CaptureStdout();
    int res = cnd_compile_file(kSourceFile, kOutFile, 0);
    testing::internal::GetCapturedStdout();
    
    EXPECT_NE(res, 0);
}
