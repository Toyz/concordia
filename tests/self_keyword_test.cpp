#include "test_common.h"
#include <fstream>

class SelfKeywordTest : public ::testing::Test {
protected:
    const char* kSourceFile = "self_test_temp.cnd";
    const char* kOutFile = "self_test_temp.il";

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

TEST_F(SelfKeywordTest, SelfKeywordCompilation) {
    WriteSource(
        "packet SelfTest {"
        "  @expr(self > 10) uint8 val;"
        "}"
    );
    int res = cnd_compile_file(kSourceFile, kOutFile, 0, 0);
    EXPECT_EQ(res, 0);
}

TEST_F(SelfKeywordTest, SelfKeywordAsFieldName) {
    WriteSource(
        "packet Fail {"
        "  uint8 self;"
        "}"
    );
    int res = cnd_compile_file(kSourceFile, kOutFile, 0, 0);
    EXPECT_NE(res, 0);
}

TEST_F(SelfKeywordTest, SelfKeywordAsFieldNameInStruct) {
    WriteSource(
        "struct Inner {"
        "  uint8 self;"
        "}"
        "packet Fail {"
        "  Inner i;"
        "}"
    );
    int res = cnd_compile_file(kSourceFile, kOutFile, 0, 0);
    EXPECT_NE(res, 0);
}
