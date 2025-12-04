#include <gtest/gtest.h>
#include <fstream>
#include <cstdio>
#include "compiler.h"

class CompilerTest : public ::testing::Test {
protected:
    const char* kSourceFile = "test_temp.cnd";
    const char* kOutFile = "test_temp.il";
    
    // For imports
    const char* kFileA = "import_a.cnd";
    const char* kFileB = "import_b.cnd";
    const char* kImportOutFile = "import_out.il";

    void TearDown() override {
        remove(kSourceFile);
        remove(kOutFile);
        remove(kFileA);
        remove(kFileB);
        remove(kImportOutFile);
    }

    void WriteSource(const std::string& content) {
        WriteFile(kSourceFile, content);
    }

    void WriteFile(const char* path, const std::string& content) {
        std::ofstream out(path);
        out << content;
        out.close();
    }

    // Returns true if file exists and size > 0
    bool CheckOutputExists(const char* path = nullptr) {
        const char* target = path ? path : kOutFile;
        std::ifstream f(target, std::ios::binary | std::ios::ate);
        return f.good() && f.tellg() > 0;
    }
};

TEST_F(CompilerTest, BasicStruct) {
    WriteSource("struct Point { float x; float y; } packet P { Point p; }");
    int res = cnd_compile_file(kSourceFile, kOutFile, 0, 0);
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
        "packet P { AllTypes t; }"
    );
    int res = cnd_compile_file(kSourceFile, kOutFile, 0, 0);
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
        "packet P { Arrays a; }"
    );
    int res = cnd_compile_file(kSourceFile, kOutFile, 0, 0);
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
        "packet P { Decorated d; }"
    );
    int res = cnd_compile_file(kSourceFile, kOutFile, 0, 0);
    EXPECT_EQ(res, 0);
    EXPECT_TRUE(CheckOutputExists());
}

TEST_F(CompilerTest, UnknownType) {
    WriteSource("struct BadType { mystery_type x; }; packet P { BadType b; }");
    
    int res = cnd_compile_file(kSourceFile, kOutFile, 0, 0);
    
    EXPECT_NE(res, 0);
}

TEST_F(CompilerTest, NestedStructs) {
    WriteSource(
        "struct Inner { uint8 val; }"
        "struct Outer { Inner i; }"
        "packet P { Outer o; }"
    );
    int res = cnd_compile_file(kSourceFile, kOutFile, 0, 0);
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
    int res = cnd_compile_file(kSourceFile, kOutFile, 0, 0);
    EXPECT_EQ(res, 0);
    EXPECT_TRUE(CheckOutputExists());
}

TEST_F(CompilerTest, BitfieldSyntax) {
    WriteSource(
        "struct Bitfields {"
        "  uint8 f1 : 1;"
        "  uint8 f2 : 3;"
        "  uint16 f3 : 12;"
        "}"
        "packet P { Bitfields b; }"
    );
    int res = cnd_compile_file(kSourceFile, kOutFile, 0, 0);
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
    int res = cnd_compile_file(kSourceFile, kOutFile, 0, 0);
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
        "packet P { Layout l; }"
    );
    int res = cnd_compile_file(kSourceFile, kOutFile, 0, 0);
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
        "packet P { Transforms t; }"
    );
    int res = cnd_compile_file(kSourceFile, kOutFile, 0, 0);
    EXPECT_EQ(res, 0);
    EXPECT_TRUE(CheckOutputExists());
}

TEST_F(CompilerTest, EmptyStruct) {
    WriteSource("struct Empty {} packet P { Empty e; }");
    int res = cnd_compile_file(kSourceFile, kOutFile, 0, 0);
    EXPECT_EQ(res, 0);
    EXPECT_TRUE(CheckOutputExists());
}

TEST_F(CompilerTest, InvalidDecorator) {
    WriteSource("struct BadDec { @nonexistent(1) uint8 x; } packet P { BadDec b; }");
    
    int res = cnd_compile_file(kSourceFile, kOutFile, 0, 0);
    
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
        "packet P { Shorthands s; }"
    );
    int res = cnd_compile_file(kSourceFile, kOutFile, 0, 0);
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
        "packet P { FillParams f; }"
    );
    int res = cnd_compile_file(kSourceFile, kOutFile, 0, 0);
    EXPECT_EQ(res, 0);
    EXPECT_TRUE(CheckOutputExists());
}

TEST_F(CompilerTest, InvalidFillParam) {
    WriteSource("struct BadFill { @fill(2) u8 x; } packet P { BadFill b; }");
    
    int res = cnd_compile_file(kSourceFile, kOutFile, 0, 0);
    
    EXPECT_NE(res, 0);
}

TEST_F(CompilerTest, MultiplePacketsFail) {
    WriteSource(
        "packet A { uint8 x; }"
        "packet B { uint8 y; }"
    );
    
    int res = cnd_compile_file(kSourceFile, kOutFile, 0, 0);
    
    EXPECT_NE(res, 0);
}

TEST_F(CompilerTest, PacketAlias) {
    WriteSource(
        "struct MyStruct { uint8 a; uint16 b; }"
        "packet MyPacket = MyStruct;"
    );
    int res = cnd_compile_file(kSourceFile, kOutFile, 0, 0);
    EXPECT_EQ(res, 0);
    EXPECT_TRUE(CheckOutputExists());
}

TEST_F(CompilerTest, PacketAliasMissingStruct) {
    WriteSource(
        "packet MyPacket = NonExistentStruct;"
    );
    int res = cnd_compile_file(kSourceFile, kOutFile, 0, 0);
    EXPECT_NE(res, 0);
}

// --- Import Tests ---

TEST_F(CompilerTest, BasicImport) {
    // File A defines a struct
    WriteFile(kFileA, "struct Point { float x; float y; }");
    
    // File B imports A and uses Point
    WriteFile(kFileB, 
        "@import(\"import_a.cnd\")"
        "packet Path { Point p1; Point p2; }"
    );
    
    int res = cnd_compile_file(kFileB, kImportOutFile, 0, 0);
    EXPECT_EQ(res, 0);
    EXPECT_TRUE(CheckOutputExists(kImportOutFile));
}

TEST_F(CompilerTest, DuplicateImport) {
    // File A defines a struct
    WriteFile(kFileA, "struct Point { float x; float y; }");
    
    // File B imports A twice (should be ignored second time)
    WriteFile(kFileB, 
        "@import(\"import_a.cnd\")"
        "@import(\"import_a.cnd\")"
        "packet Path { Point p1; Point p2; }"
    );
    
    int res = cnd_compile_file(kFileB, kImportOutFile, 0, 0);
    EXPECT_EQ(res, 0);
    EXPECT_TRUE(CheckOutputExists(kImportOutFile));
}

TEST_F(CompilerTest, CircularImport) {
    // File A imports B
    WriteFile(kFileA, "@import(\"import_b.cnd\") struct A { uint8 x; }");
    
    // File B imports A
    WriteFile(kFileB, "@import(\"import_a.cnd\") struct B { uint8 y; }");
    
    // This should fail or handle gracefully depending on implementation
    // Current implementation detects recursion depth or just fails to find types if not loaded
    // Assuming it should fail for now as circular deps are hard
    int res = cnd_compile_file(kFileA, kImportOutFile, 0, 0);
    EXPECT_NE(res, 0);
}

TEST_F(CompilerTest, MissingImport) {
    WriteFile(kFileB, 
        "@import(\"non_existent.cnd\")"
        "packet P { uint8 x; }"
    );
    
    int res = cnd_compile_file(kFileB, kImportOutFile, 0, 0);
    EXPECT_NE(res, 0);
}

// --- Name Collision Tests ---

TEST_F(CompilerTest, DuplicateStruct) {
    WriteSource(
        "struct Point { float x; float y; }"
        "struct Point { float z; }" // Duplicate
        "packet P { Point p; }"
    );
    int res = cnd_compile_file(kSourceFile, kOutFile, 0, 0);
    EXPECT_NE(res, 0);
}

TEST_F(CompilerTest, DuplicateEnum) {
    WriteSource(
        "enum Color { RED, GREEN, BLUE }"
        "enum Color { CYAN, MAGENTA, YELLOW }" // Duplicate
        "packet P { Color c; }"
    );
    int res = cnd_compile_file(kSourceFile, kOutFile, 0, 0);
    EXPECT_NE(res, 0);
}

TEST_F(CompilerTest, DuplicatePacket) {
    // Note: Multiple packets are already disallowed, but this checks name collision specifically
    // if we were to allow multiple packets or if it conflicts with struct
    WriteSource(
        "struct Data { uint8 x; }"
        "packet Data { uint8 y; }" // Collision with struct
    );
    int res = cnd_compile_file(kSourceFile, kOutFile, 0, 0);
    EXPECT_NE(res, 0);
}

TEST_F(CompilerTest, EnumValueCollision) {
    WriteSource(
        "enum Status { OK = 0, ERROR = 1, OK = 2 }" // Duplicate key
        "packet P { Status s; }"
    );
    int res = cnd_compile_file(kSourceFile, kOutFile, 0, 0);
    EXPECT_NE(res, 0);
}

TEST_F(CompilerTest, FieldNameCollision) {
    WriteSource(
        "struct Point {"
        "  float x;"
        "  float y;"
        "  float x;" // Duplicate field
        "}"
        "packet P { Point p; }"
    );
    int res = cnd_compile_file(kSourceFile, kOutFile, 0, 0);
    EXPECT_NE(res, 0);
}

// --- Self Keyword Tests ---

TEST_F(CompilerTest, SelfKeywordCompilation) {
    WriteSource(
        "packet SelfTest {"
        "  @expr(self > 10) uint8 val;"
        "}"
    );
    int res = cnd_compile_file(kSourceFile, kOutFile, 0, 0);
    EXPECT_EQ(res, 0);
}
