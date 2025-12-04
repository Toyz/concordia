#include <gtest/gtest.h>
#include <fstream>
#include <cstdio>
#include "compiler.h"

class NameCollisionTest : public ::testing::Test {
protected:
    const char* kSourceFile = "collision_temp.cnd";
    const char* kOutFile = "collision_temp.il";

    void TearDown() override {
        remove(kSourceFile);
        remove(kOutFile);
    }

    void WriteSource(const std::string& content) {
        std::ofstream out(kSourceFile);
        out << content;
        out.close();
    }
};

TEST_F(NameCollisionTest, DuplicateStruct) {
    WriteSource(
        "struct Point { float x; float y; }"
        "struct Point { float z; }" // Duplicate
        "packet P { Point p; }"
    );
    int res = cnd_compile_file(kSourceFile, kOutFile, 0, 0);
    EXPECT_NE(res, 0);
}

TEST_F(NameCollisionTest, DuplicateEnum) {
    WriteSource(
        "enum Color { RED, GREEN }"
        "enum Color { BLUE }" // Duplicate
        "packet P { Color c; }"
    );
    int res = cnd_compile_file(kSourceFile, kOutFile, 0, 0);
    EXPECT_NE(res, 0);
}

TEST_F(NameCollisionTest, StructEnumCollision) {
    WriteSource(
        "struct Thing { float x; }"
        "enum Thing { A, B }" // Collision
        "packet P { Thing t; }"
    );
    int res = cnd_compile_file(kSourceFile, kOutFile, 0, 0);
    EXPECT_NE(res, 0);
}

TEST_F(NameCollisionTest, EnumStructCollision) {
    WriteSource(
        "enum Thing { A, B }"
        "struct Thing { float x; }" // Collision
        "packet P { Thing t; }"
    );
    int res = cnd_compile_file(kSourceFile, kOutFile, 0, 0);
    EXPECT_NE(res, 0);
}

TEST_F(NameCollisionTest, PacketStructCollision) {
    WriteSource(
        "struct Data { float x; }"
        "packet Data { Data d; }" // Collision
    );
    int res = cnd_compile_file(kSourceFile, kOutFile, 0, 0);
    EXPECT_NE(res, 0);
}

TEST_F(NameCollisionTest, PacketEnumCollision) {
    WriteSource(
        "enum Type { A, B }"
        "packet Type { Type t; }" // Collision
    );
    int res = cnd_compile_file(kSourceFile, kOutFile, 0, 0);
    EXPECT_NE(res, 0);
}
