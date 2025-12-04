#include <gtest/gtest.h>
#include "../src/compiler/cnd_internal.h"

class LexerTest : public ::testing::Test {
protected:
    Lexer lexer;
    
    void Init(const char* source) {
        lexer_init(&lexer, source);
    }
    
    Token Next() {
        return lexer_next(&lexer);
    }
};

TEST_F(LexerTest, BasicTokens) {
    Init("struct packet enum switch case default true false { } [ ] ( ) ; : , @ = .");
    
    EXPECT_EQ(Next().type, TOK_STRUCT);
    EXPECT_EQ(Next().type, TOK_PACKET);
    EXPECT_EQ(Next().type, TOK_ENUM);
    EXPECT_EQ(Next().type, TOK_SWITCH);
    EXPECT_EQ(Next().type, TOK_CASE);
    EXPECT_EQ(Next().type, TOK_DEFAULT);
    EXPECT_EQ(Next().type, TOK_TRUE);
    EXPECT_EQ(Next().type, TOK_FALSE);
    EXPECT_EQ(Next().type, TOK_LBRACE);
    EXPECT_EQ(Next().type, TOK_RBRACE);
    EXPECT_EQ(Next().type, TOK_LBRACKET);
    EXPECT_EQ(Next().type, TOK_RBRACKET);
    EXPECT_EQ(Next().type, TOK_LPAREN);
    EXPECT_EQ(Next().type, TOK_RPAREN);
    EXPECT_EQ(Next().type, TOK_SEMICOLON);
    EXPECT_EQ(Next().type, TOK_COLON);
    EXPECT_EQ(Next().type, TOK_COMMA);
    EXPECT_EQ(Next().type, TOK_AT);
    EXPECT_EQ(Next().type, TOK_EQUALS);
    EXPECT_EQ(Next().type, TOK_DOT);
    EXPECT_EQ(Next().type, TOK_EOF);
}

TEST_F(LexerTest, Numbers) {
    Init("123 0 0x1A -5 -0xFF 3.14 0.5 -2.0");
    
    Token t;
    
    // 123
    t = Next(); EXPECT_EQ(t.type, TOK_NUMBER); 
    EXPECT_EQ(std::string(t.start, t.length), "123");
    
    // 0
    t = Next(); EXPECT_EQ(t.type, TOK_NUMBER); 
    EXPECT_EQ(std::string(t.start, t.length), "0");
    
    // 0x1A
    t = Next(); EXPECT_EQ(t.type, TOK_NUMBER); 
    EXPECT_EQ(std::string(t.start, t.length), "0x1A");
    
    // -5
    t = Next(); EXPECT_EQ(t.type, TOK_NUMBER); 
    EXPECT_EQ(std::string(t.start, t.length), "-5");
    
    // -0xFF
    t = Next(); EXPECT_EQ(t.type, TOK_NUMBER); 
    EXPECT_EQ(std::string(t.start, t.length), "-0xFF");
    
    // 3.14
    t = Next(); EXPECT_EQ(t.type, TOK_NUMBER); 
    EXPECT_EQ(std::string(t.start, t.length), "3.14");
    
    // 0.5
    t = Next(); EXPECT_EQ(t.type, TOK_NUMBER); 
    EXPECT_EQ(std::string(t.start, t.length), "0.5");
    
    // -2.0
    t = Next(); EXPECT_EQ(t.type, TOK_NUMBER); 
    EXPECT_EQ(std::string(t.start, t.length), "-2.0");
    
    EXPECT_EQ(Next().type, TOK_EOF);
}

TEST_F(LexerTest, Strings) {
    Init("\"hello\" \"world\"");
    
    Token t;
    t = Next(); 
    EXPECT_EQ(t.type, TOK_STRING);
    EXPECT_EQ(std::string(t.start, t.length), "hello");
    
    t = Next();
    EXPECT_EQ(t.type, TOK_STRING);
    EXPECT_EQ(std::string(t.start, t.length), "world");
}

TEST_F(LexerTest, Comments) {
    Init("struct // This is a comment\npacket");
    
    EXPECT_EQ(Next().type, TOK_STRUCT); // struct
    // Comment skipped
    EXPECT_EQ(Next().type, TOK_PACKET); // packet
    EXPECT_EQ(Next().type, TOK_EOF);
}

TEST_F(LexerTest, Whitespace) {
    Init("   \t\n  x  \n");
    Token t = Next();
    EXPECT_EQ(t.type, TOK_IDENTIFIER);
    EXPECT_EQ(std::string(t.start, t.length), "x");
    EXPECT_EQ(Next().type, TOK_EOF);
}

TEST_F(LexerTest, InvalidTokens) {
    Init("$invalid #token");
    
    EXPECT_EQ(Next().type, TOK_ERROR);
    EXPECT_EQ(Next().type, TOK_IDENTIFIER);
    EXPECT_EQ(Next().type, TOK_ERROR);
    EXPECT_EQ(Next().type, TOK_IDENTIFIER);
    EXPECT_EQ(Next().type, TOK_EOF);
}

TEST_F(LexerTest, EmptyInput) {
    Init("");
    EXPECT_EQ(Next().type, TOK_EOF);
}

TEST_F(LexerTest, UnterminatedString) {
    Init("\"unterminated");
    EXPECT_EQ(Next().type, TOK_STRING); // Lexer will treat it as a string until EOF
    EXPECT_EQ(Next().type, TOK_EOF);
}

TEST_F(LexerTest, UnterminatedBlockComment) {
    Init("/* unterminated comment");
    EXPECT_EQ(Next().type, TOK_EOF); // Lexer skips the comment until EOF
}
