#include <gtest/gtest.h>
#include <fstream>
#include <cstdio>
#include "compiler.h"

class ValidationTest : public ::testing::Test {
protected:
    const char* kSourceFile = "validation_temp.cnd";
    const char* kOutFile = "validation_temp.il";

    void TearDown() override {
        remove(kSourceFile);
        remove(kOutFile);
    }

    void WriteSource(const std::string& content) {
        std::ofstream out(kSourceFile);
        out << content;
        out.close();
    }

    bool CompileShouldFail(const std::string& source) {
        WriteSource(source);
        testing::internal::CaptureStdout(); // Suppress error output
        int res = cnd_compile_file(kSourceFile, kOutFile, 0);
        testing::internal::GetCapturedStdout();
        return res != 0;
    }
};

TEST_F(ValidationTest, ScaleOnString) {
    EXPECT_TRUE(CompileShouldFail("struct S { @scale(1.0) string s; }"));
}

TEST_F(ValidationTest, RangeOnString) {
    EXPECT_TRUE(CompileShouldFail("struct S { @range(0, 10) string s; }"));
}

TEST_F(ValidationTest, BitfieldOnFloat) {
    EXPECT_TRUE(CompileShouldFail("struct S { float f : 4; }"));
}

TEST_F(ValidationTest, BitfieldOnString) {
    EXPECT_TRUE(CompileShouldFail("struct S { string s : 4; }"));
}

TEST_F(ValidationTest, CRCOnString) {
    EXPECT_TRUE(CompileShouldFail("packet P { @crc(32) string s; }"));
}

TEST_F(ValidationTest, InvalidRangeArgs) {
    // Min > Max
    EXPECT_TRUE(CompileShouldFail("struct S { @range(10, 0) int x; }"));
}

TEST_F(ValidationTest, DuplicateField) {
    EXPECT_TRUE(CompileShouldFail("struct S { int x; int x; }"));
}

TEST_F(ValidationTest, RecursiveStruct) {
    EXPECT_TRUE(CompileShouldFail("struct S { S s; }"));
}

TEST_F(ValidationTest, InvalidConstType) {
    // String literal for int const
    EXPECT_TRUE(CompileShouldFail("struct S { @const(\"abc\") int x; }"));
}

TEST_F(ValidationTest, ScaleTypeMismatch) {
    // User requested: "using @scale with a float value but the type is an int"
    // As discussed, @scale(0.1) int x is VALID (Fixed Point).
    // But maybe @scale on a type that doesn't support math?
    // We already tested ScaleOnString.
    // What about @scale on a struct?
    EXPECT_TRUE(CompileShouldFail("struct Inner { int x; } struct S { @scale(2.0) Inner i; }"));
}

TEST_F(ValidationTest, ArrayPrefixTypeMismatch) {
    // Prefix must be integer
    EXPECT_TRUE(CompileShouldFail("struct S { int arr[] prefix float; }"));
}
