#include <gtest/gtest.h>
#include <fstream>
#include <cstdio>
#include "compiler.h"

class ImportTest : public ::testing::Test {
protected:
    const char* kFileA = "import_a.cnd";
    const char* kFileB = "import_b.cnd";
    const char* kOutFile = "import_out.il";

    void TearDown() override {
        remove(kFileA);
        remove(kFileB);
        remove(kOutFile);
    }

    void WriteFile(const char* path, const std::string& content) {
        std::ofstream out(path);
        out << content;
        out.close();
    }

    bool CheckOutputExists() {
        std::ifstream f(kOutFile, std::ios::binary | std::ios::ate);
        return f.good() && f.tellg() > 0;
    }
};

TEST_F(ImportTest, BasicImport) {
    // File A defines a struct
    WriteFile(kFileA, "struct Point { float x; float y; }");
    
    // File B imports A and uses Point
    WriteFile(kFileB, 
        "@import(\"import_a.cnd\")"
        "packet Path { Point p1; Point p2; }"
    );
    
    int res = cnd_compile_file(kFileB, kOutFile, 0);
    EXPECT_EQ(res, 0);
    EXPECT_TRUE(CheckOutputExists());
}

TEST_F(ImportTest, DuplicateImport) {
    // File A defines a struct
    WriteFile(kFileA, "struct Point { float x; float y; }");
    
    // File B imports A twice (should be ignored second time)
    WriteFile(kFileB, 
        "@import(\"import_a.cnd\")"
        "@import(\"import_a.cnd\")"
        "packet Path { Point p1; }"
    );
    
    int res = cnd_compile_file(kFileB, kOutFile, 0);
    EXPECT_EQ(res, 0);
    EXPECT_TRUE(CheckOutputExists());
}

TEST_F(ImportTest, MissingFile) {
    WriteFile(kFileB, "@import(\"non_existent.cnd\")");
    
    testing::internal::CaptureStdout();
    int res = cnd_compile_file(kFileB, kOutFile, 0);
    testing::internal::GetCapturedStdout();
    
    EXPECT_NE(res, 0);
}
