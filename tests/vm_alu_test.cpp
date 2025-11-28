#include <gtest/gtest.h>
#include "test_common.h"
#include "../src/vm/vm_internal.h"

class VmAluTest : public ::testing::Test {
protected:
    cnd_vm_ctx ctx;
    cnd_program program;
    uint8_t data[1024];
    uint8_t bytecode[1024];
    
    void SetUp() override {
        memset(&ctx, 0, sizeof(ctx));
        memset(&program, 0, sizeof(program));
        memset(data, 0, sizeof(data));
        memset(bytecode, 0, sizeof(bytecode));
        
        program.bytecode = bytecode;
        program.bytecode_len = sizeof(bytecode);
        
        cnd_init(&ctx, CND_MODE_ENCODE, &program, data, sizeof(data), NULL, NULL);
    }

    void Run(size_t len) {
        program.bytecode_len = len;
        ASSERT_EQ(cnd_execute(&ctx), CND_ERR_OK);
    }
};

TEST_F(VmAluTest, StackPushPop) {
    // PUSH_IMM 42, POP
    uint8_t* p = bytecode;
    *p++ = OP_PUSH_IMM;
    *(uint64_t*)p = 42; p += 8;
    *p++ = OP_POP;
    
    Run(p - bytecode);
    EXPECT_EQ(ctx.expr_sp, 0);
}

TEST_F(VmAluTest, BitwiseAnd) {
    // PUSH 0x0F, PUSH 0x03, AND -> 0x03
    uint8_t* p = bytecode;
    *p++ = OP_PUSH_IMM; *(uint64_t*)p = 0x0F; p += 8;
    *p++ = OP_PUSH_IMM; *(uint64_t*)p = 0x03; p += 8;
    *p++ = OP_BIT_AND;
    
    Run(p - bytecode);
    EXPECT_EQ(ctx.expr_sp, 1);
    EXPECT_EQ(ctx.expr_stack[0], 0x03);
}

TEST_F(VmAluTest, ComparisonEq) {
    // PUSH 10, PUSH 10, EQ -> 1
    uint8_t* p = bytecode;
    *p++ = OP_PUSH_IMM; *(uint64_t*)p = 10; p += 8;
    *p++ = OP_PUSH_IMM; *(uint64_t*)p = 10; p += 8;
    *p++ = OP_EQ;
    
    Run(p - bytecode);
    EXPECT_EQ(ctx.expr_sp, 1);
    EXPECT_EQ(ctx.expr_stack[0], 1);
}

TEST_F(VmAluTest, ComparisonNeq) {
    // PUSH 10, PUSH 20, NEQ -> 1
    uint8_t* p = bytecode;
    *p++ = OP_PUSH_IMM; *(uint64_t*)p = 10; p += 8;
    *p++ = OP_PUSH_IMM; *(uint64_t*)p = 20; p += 8;
    *p++ = OP_NEQ;
    
    Run(p - bytecode);
    EXPECT_EQ(ctx.expr_sp, 1);
    EXPECT_EQ(ctx.expr_stack[0], 1);
}

TEST_F(VmAluTest, LogicalNot) {
    // PUSH 0, NOT -> 1
    uint8_t* p = bytecode;
    *p++ = OP_PUSH_IMM; *(uint64_t*)p = 0; p += 8;
    *p++ = OP_LOG_NOT;
    
    Run(p - bytecode);
    EXPECT_EQ(ctx.expr_sp, 1);
    EXPECT_EQ(ctx.expr_stack[0], 1);
    
    // Reset
    ctx.expr_sp = 0;
    ctx.ip = 0;
    
    // PUSH 1, NOT -> 0
    p = bytecode;
    *p++ = OP_PUSH_IMM; *(uint64_t*)p = 1; p += 8;
    *p++ = OP_LOG_NOT;
    
    Run(p - bytecode);
    EXPECT_EQ(ctx.expr_sp, 1);
    EXPECT_EQ(ctx.expr_stack[0], 0);
}

TEST_F(VmAluTest, JumpIfNot) {
    uint8_t bc[] = {
        OP_PUSH_IMM, 0, 0, 0, 0, 0, 0, 0, 0,
        OP_JUMP_IF_NOT, 9, 0, 0, 0, // Jump 9 bytes forward
        OP_PUSH_IMM, 1, 0, 0, 0, 0, 0, 0, 0, // Skipped
        OP_PUSH_IMM, 2, 0, 0, 0, 0, 0, 0, 0  // Target
    };
    
    cnd_program_load(&program, bc, sizeof(bc));
    ctx.program = &program;
    
    EXPECT_EQ(cnd_execute(&ctx), CND_ERR_OK);
    
    // Stack should have 2 (and not 1)
    EXPECT_EQ(ctx.expr_sp, 1);
    EXPECT_EQ(ctx.expr_stack[0], 2);
}

TEST_F(VmAluTest, JumpIfNotTaken) {
    uint8_t bc[] = {
        OP_PUSH_IMM, 1, 0, 0, 0, 0, 0, 0, 0,
        OP_JUMP_IF_NOT, 9, 0, 0, 0, // Jump 9 bytes forward
        OP_PUSH_IMM, 1, 0, 0, 0, 0, 0, 0, 0, // Executed
        OP_PUSH_IMM, 2, 0, 0, 0, 0, 0, 0, 0  // Executed
    };
    
    cnd_program_load(&program, bc, sizeof(bc));
    ctx.program = &program;
    
    EXPECT_EQ(cnd_execute(&ctx), CND_ERR_OK);
    
    // Stack should have 1, 2
    EXPECT_EQ(ctx.expr_sp, 2);
    EXPECT_EQ(ctx.expr_stack[0], 1);
    EXPECT_EQ(ctx.expr_stack[1], 2);
}
