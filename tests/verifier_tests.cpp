#include <gtest/gtest.h>
#include "concordia.h"

class VerifierTest : public ::testing::Test {
protected:
    void SetUp() override {
    }

    void TearDown() override {
    }
};

TEST_F(VerifierTest, ValidProgram) {
    // Simple program: PUSH 10
    uint8_t bytecode[] = {
        OP_PUSH_IMM, 10, 0, 0, 0, 0, 0, 0, 0
    };
    cnd_program prog;
    prog.bytecode = bytecode;
    prog.bytecode_len = sizeof(bytecode);
    prog.string_table = nullptr;
    prog.string_count = 0;

    EXPECT_EQ(cnd_verify_program(&prog), CND_ERR_OK);
}

TEST_F(VerifierTest, InvalidOpcode) {
    uint8_t bytecode[] = {
        0xFF // Invalid opcode
    };
    cnd_program prog;
    prog.bytecode = bytecode;
    prog.bytecode_len = sizeof(bytecode);

    EXPECT_EQ(cnd_verify_program(&prog), CND_ERR_INVALID_OP);
}

TEST_F(VerifierTest, OOB_Arg) {
    // PUSH instruction cut off
    uint8_t bytecode[] = {
        OP_PUSH_IMM, 10, 0 // Missing bytes
    };
    cnd_program prog;
    prog.bytecode = bytecode;
    prog.bytecode_len = sizeof(bytecode);

    EXPECT_EQ(cnd_verify_program(&prog), CND_ERR_OOB);
}

TEST_F(VerifierTest, OOB_StringID) {
    // Skipped
}

TEST_F(VerifierTest, SwitchTable_Valid) {
    // OP_SWITCH_TABLE
    // Op(1) + Key(2) + Rel(4)
    // Table: Min(8) + Max(8) + Def(4) + Offsets(4*2)
    
    uint8_t bytecode[] = {
        OP_SWITCH_TABLE, 0, 0, // Key 0
        0, 0, 0, 0, // Rel offset 0 (Table starts immediately after instruction)
        
        // Table (at offset 7)
        0, 0, 0, 0, 0, 0, 0, 0, // Min = 0
        1, 0, 0, 0, 0, 0, 0, 0, // Max = 1 (Count = 2)
        0, 0, 0, 0, // Default offset = 0 (Jumps to start of instruction + 7 + 0 = Table Start) - Valid but infinite loop
        
        // Offsets
        0, 0, 0, 0, // Offset 0
        0, 0, 0, 0  // Offset 1
    };
    
    cnd_program prog;
    prog.bytecode = bytecode;
    prog.bytecode_len = sizeof(bytecode);

    EXPECT_EQ(cnd_verify_program(&prog), CND_ERR_OK);
}

TEST_F(VerifierTest, SwitchTable_OOB) {
    // OP_SWITCH_TABLE with OOB offset
    uint8_t bytecode[] = {
        OP_SWITCH_TABLE, 0, 0,
        0, 0, 0, 0, // Rel 0
        
        // Table
        0, 0, 0, 0, 0, 0, 0, 0, // Min 0
        0, 0, 0, 0, 0, 0, 0, 0, // Max 0 (Count 1)
        0, 0, 0, 0, // Default 0
        
        // Offset 0: Huge forward jump
        0xFF, 0xFF, 0xFF, 0x7F // INT32_MAX
    };
    
    cnd_program prog;
    prog.bytecode = bytecode;
    prog.bytecode_len = sizeof(bytecode);

    EXPECT_EQ(cnd_verify_program(&prog), CND_ERR_OOB);
}

TEST_F(VerifierTest, Switch_Valid) {
    // OP_SWITCH
    // Op(1) + Key(2) + Rel(4)
    // Table: Count(2) + Def(4) + [Val(8) + Off(4)]
    
    uint8_t bytecode[] = {
        OP_SWITCH, 0, 0,
        0, 0, 0, 0, // Rel 0
        
        // Table
        1, 0, // Count 1
        0, 0, 0, 0, // Default 0
        
        // Case 0
        5, 0, 0, 0, 0, 0, 0, 0, // Val 5
        0, 0, 0, 0 // Offset 0
    };
    
    cnd_program prog;
    prog.bytecode = bytecode;
    prog.bytecode_len = sizeof(bytecode);

    EXPECT_EQ(cnd_verify_program(&prog), CND_ERR_OK);
}
