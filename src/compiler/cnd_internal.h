#ifndef CND_INTERNAL_H
#define CND_INTERNAL_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <ctype.h>
#include "../../include/concordia.h"
#include "../../include/compiler.h"

#ifdef __cplusplus
extern "C" {
#endif

// --- Tokens ---
typedef enum {
    TOK_EOF,
    TOK_IDENTIFIER,
    TOK_NUMBER,
    TOK_STRING,
    TOK_LBRACE,    // {
    TOK_RBRACE,    // }
    TOK_LBRACKET,  // [
    TOK_RBRACKET,  // ]
    TOK_LPAREN,    // (
    TOK_RPAREN,    // )
    TOK_SEMICOLON, // ;
    TOK_COLON,     // :
    TOK_COMMA,     // ,
    TOK_AT         // @
} TokenType;

typedef struct {
    TokenType type;
    const char* start;
    int length;
    int line;
} Token;

// --- Lexer ---
typedef struct {
    const char* source;
    const char* current;
    int line;
} Lexer;

void lexer_init(Lexer* lexer, const char* source);
Token lexer_next(Lexer* lexer);

// --- Utils: Buffer ---
typedef struct {
    uint8_t* data;
    size_t size;
    size_t capacity;
} Buffer;

void buf_init(Buffer* b);
void buf_free(Buffer* b);
void buf_append(Buffer* b, const uint8_t* data, size_t len);
void buf_push(Buffer* b, uint8_t byte);
void buf_push_u16(Buffer* b, uint16_t val);
void buf_push_u32(Buffer* b, uint32_t val);
void buf_push_u64(Buffer* b, uint64_t val);
void buf_write_u16_at(Buffer* b, size_t offset, uint16_t val);

// --- Utils: String Table ---
typedef struct {
    char** strings;
    size_t count;
    size_t capacity;
} StringTable;

void strtab_init(StringTable* t);
uint16_t strtab_add(StringTable* t, const char* start, int len);

// --- Utils: Registry ---
typedef struct {
    char* name;
    Buffer bytecode;
} StructDef;

typedef struct {
    StructDef* defs;
    size_t count;
    size_t capacity;
} StructRegistry;

void reg_init(StructRegistry* r);
StructDef* reg_add(StructRegistry* r, const char* name, int len);
StructDef* reg_find(StructRegistry* r, const char* name, int len);

// --- Parser ---
typedef struct {
    Lexer lexer;
    Token current;
    Token previous;
    Buffer* target;     // Current buffer being emitted to (Global or Struct)
    Buffer global_bc;   // Top-level packet bytecode
    StringTable strtab;
    StructRegistry registry;
    StringTable imports; // Track imported files
    const char* current_path; // Current file path for relative imports
    const char* current_struct_name; // Name of the struct currently being parsed (for recursion check)
    int current_struct_name_len;
    int had_error;
    int json_output; // Flag for JSON error output
} Parser;

void parser_error(Parser* p, const char* msg);
void advance(Parser* p);
void consume(Parser* p, TokenType type, const char* msg);
int match_keyword(Token t, const char* kw);
uint32_t parse_number(Token t);
int64_t parse_int64(Token t);
double parse_double(Token t);

void parse_top_level(Parser* p);

#ifdef __cplusplus
}
#endif

#endif // CND_INTERNAL_H
