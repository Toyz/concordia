#include "test_common.h"
#include <fstream>

class ImportExecutionTest : public ConcordiaTest {
protected:
    const char* kAuxFile = "defs.cnd";

    void TearDown() override {
        remove(kAuxFile);
        ConcordiaTest::TearDown();
    }

    void WriteAuxFile(const std::string& content) {
        std::ofstream out(kAuxFile);
        out << content;
        out.close();
    }
};

TEST_F(ImportExecutionTest, StructImportExecution) {
    // 1. Define a struct in an auxiliary file
    WriteAuxFile(
        "struct Vec2 {"
        "  float x;"
        "  float y;"
        "}"
    );

    // 2. Compile and load a packet that imports it
    // Note: We use @import("defs.cnd")
    CompileAndLoad(
        "@import(\"defs.cnd\")"
        "packet GameData {"
        "  Vec2 position;"
        "  Vec2 velocity;"
        "}"
    );

    // 3. Setup Test Data for Encoding
    // We expect 4 floats: pos.x, pos.y, vel.x, vel.y
    // We use Tape Mode with wildcard keys (0xFFFF) to provide 0.0 for all 4 fields.
    
    clear_test_data();
    tctx.use_tape = true;
    tctx.tape_index = 0;
    
    for(int i=0; i<4; i++) {
        g_test_data[i].key = 0xFFFF; // Match any key
        g_test_data[i].u64_val = 0;  // Value 0
    }
    
    uint8_t buffer[128];
    memset(buffer, 0xFF, sizeof(buffer));
    
    // ENCODE (All zeros)
    cnd_init(&ctx, CND_MODE_ENCODE, &program, buffer, sizeof(buffer), test_io_callback, &tctx);
    EXPECT_EQ(cnd_execute(&ctx), CND_ERR_OK);
    
    // Verify size: 4 floats * 4 bytes = 16 bytes
    // Check that the first 16 bytes are 0
    for(int i=0; i<16; i++) {
        EXPECT_EQ(buffer[i], 0);
    }
    // Byte 17 should be 0xFF (untouched)
    EXPECT_EQ(buffer[16], 0xFF);
}
