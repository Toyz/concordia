#ifndef CND_INTERNAL_H
#define CND_INTERNAL_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <ctype.h>
#include "concordia.h"
#include "compiler.h"

#ifdef _WIN32
#define strdup _strdup
#endif

#ifdef __cplusplus
extern "C" {
#endif

// ANSI Color Codes
#define COLOR_RESET   "\033[0m"
#define COLOR_RED     "\033[31m"
#define COLOR_GREEN   "\033[32m"
#define COLOR_YELLOW  "\033[33m"
#define COLOR_BLUE    "\033[34m"
#define COLOR_MAGENTA "\033[35m"
#define COLOR_CYAN    "\033[36m"
#define COLOR_BOLD    "\033[1m"

// --- Tokens ---
typedef enum {
    TOK_EOF,
    TOK_IDENTIFIER,
    TOK_NUMBER,
    TOK_STRING,
    
    // Keywords
    TOK_STRUCT,
    TOK_PACKET,
    TOK_ENUM,
    TOK_SWITCH,
    TOK_CASE,
    TOK_DEFAULT,
    TOK_IF,
    TOK_ELSE,
    TOK_TRUE,
    TOK_FALSE,
    TOK_SELF,

    TOK_LBRACE,    // {
    TOK_RBRACE,    // }
    TOK_LBRACKET,  // [
    TOK_RBRACKET,  // ]
    TOK_LPAREN,    // (
    TOK_RPAREN,    // )
    TOK_SEMICOLON, // ;
    TOK_COLON,     // :
    TOK_COMMA,     // ,
    TOK_AT,        // @
    TOK_EQUALS,    // =
    TOK_DOT,       // .
    TOK_BANG,      // !
    TOK_AMP,       // &
    TOK_PIPE,      // |
    TOK_CARET,     // ^
    TOK_TILDE,     // ~
    TOK_EQ_EQ,     // ==
    TOK_BANG_EQ,   // !=
    TOK_GT,        // >
    TOK_LT,        // <
    TOK_GT_EQ,     // >=
    TOK_LT_EQ,     // <=
    TOK_AMP_AMP,   // &&
    TOK_PIPE_PIPE, // ||
    TOK_LSHIFT,    // <<
    TOK_RSHIFT,    // >>
    TOK_PLUS,      // +
    TOK_MINUS,     // -
    TOK_STAR,      // *
    TOK_SLASH,     // /
    TOK_PERCENT,   // %
    TOK_DOC_COMMENT, // /// Comment
    TOK_ERROR      // Lexer error
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
void buf_write_u32_at(Buffer* b, size_t offset, uint32_t val);
void buf_write_u8_at(Buffer* b, size_t offset, uint8_t val);
size_t buf_current_offset(Buffer* b);

// --- Utils: String Table ---
typedef struct {
    char** strings;
    size_t count;
    size_t capacity;
} StringTable;

void strtab_init(StringTable* t);
void strtab_free(StringTable* t);
uint16_t strtab_add(StringTable* t, const char* start, int len);

// --- Utils: Registry ---
typedef struct {
    char* name;
    Buffer bytecode;
    int line;
    char* file; // File path where defined
    char* doc_comment;
} StructDef;

typedef struct {
    StructDef* defs;
    size_t count;
    size_t capacity;
} StructRegistry;

void reg_init(StructRegistry* r);
void reg_free(StructRegistry* r);
StructDef* reg_add(StructRegistry* r, const char* name, int len, int line, const char* file, const char* doc);
StructDef* reg_find(StructRegistry* r, const char* name, int len);

// --- Utils: Enum Registry ---
typedef struct {
    char* name;
    int64_t value;
    char* doc_comment;
} EnumValue;

typedef struct {
    char* name;
    uint8_t underlying_type; // OP_IO_U8, OP_IO_I32, etc.
    EnumValue* values;
    size_t count;
    size_t capacity;
    int line;
    char* file; // File path where defined
    char* doc_comment;
} EnumDef;

typedef struct {
    EnumDef* defs;
    size_t count;
    size_t capacity;
} EnumRegistry;

void enum_reg_init(EnumRegistry* r);
void enum_reg_free(EnumRegistry* r);
EnumDef* enum_reg_add(EnumRegistry* r, const char* name, int len, int line, const char* file, const char* doc);
EnumDef* enum_reg_find(EnumRegistry* r, const char* name, int len);

// --- Parser ---
typedef struct {
    int line;
    int column;
    char* message;
} CompilerError;

typedef struct {
    Lexer lexer;
    Token current;
    Token previous;
    Buffer* target;     // Current buffer being emitted to (Global or Struct)
    Buffer global_bc;   // Top-level packet bytecode
    StringTable strtab;
    StructRegistry registry;
    EnumRegistry enums;
    StringTable imports; // Track imported files
    const char* current_path; // Current file path for relative imports
    const char* current_struct_name; // Name of the struct currently being parsed (for recursion check)
    int current_struct_name_len;
    int had_error;
    int error_count; // Total number of errors encountered
    int json_output; // Flag for JSON error output
    int silent;      // Flag to suppress all output (for LSP)
    int verbose;     // Flag for verbose debug output
    int packet_count; // Track number of packets defined
    int import_depth; // Track recursion depth of imports
    
    // Decorator State
    int pending_unaligned; // Flag: Next struct is @unaligned_bytes
    int pending_be;        // Flag: Next struct is @big_endian
    int pending_le;        // Flag: Next struct is @little_endian
    int in_bit_mode;       // Flag: Currently parsing inside @unaligned_bytes struct
    
    // Bit Tracking for Validation
    int current_bit_count; // Bits consumed in current struct
    int is_bit_count_valid; // 1 if bit count is deterministic, 0 if dynamic (loops/ifs)

    CompilerError* errors; // List of errors for LSP
    size_t error_cap;
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
