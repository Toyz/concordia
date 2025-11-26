#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <ctype.h>
#include "../../include/concordia.h"
#include "../../include/compiler.h"

// --- Data Structures ---

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
    TOK_AT         // @
} TokenType;

typedef struct {
    TokenType type;
    const char* start;
    int length;
    int line;
} Token;

typedef struct {
    const char* source;
    const char* current;
    int line;
} Lexer;

// --- Buffers ---

typedef struct {
    uint8_t* data;
    size_t size;
    size_t capacity;
} Buffer;

void buf_init(Buffer* b) {
    b->size = 0;
    b->capacity = 1024;
    b->data = malloc(b->capacity);
}

void buf_append(Buffer* b, const uint8_t* data, size_t len) {
    if (b->size + len > b->capacity) {
        while (b->size + len > b->capacity) b->capacity *= 2;
        b->data = realloc(b->data, b->capacity);
    }
    memcpy(b->data + b->size, data, len);
    b->size += len;
}

void buf_push(Buffer* b, uint8_t byte) {
    if (b->size >= b->capacity) {
        b->capacity *= 2;
        b->data = realloc(b->data, b->capacity);
    }
    b->data[b->size++] = byte;
}

void buf_push_u16(Buffer* b, uint16_t val) {
    buf_push(b, val & 0xFF);
    buf_push(b, (val >> 8) & 0xFF);
}

void buf_push_u32(Buffer* b, uint32_t val) {
    buf_push(b, val & 0xFF);
    buf_push(b, (val >> 8) & 0xFF);
    buf_push(b, (val >> 16) & 0xFF);
    buf_push(b, (val >> 24) & 0xFF);
}

void buf_free(Buffer* b) {
    free(b->data);
}

// --- String Table ---

typedef struct {
    char** strings;
    size_t count;
    size_t capacity;
} StringTable;

void strtab_init(StringTable* t) {
    t->count = 0;
    t->capacity = 32;
    t->strings = malloc(t->capacity * sizeof(char*));
}

uint16_t strtab_add(StringTable* t, const char* start, int len) {
    for (size_t i = 0; i < t->count; i++) {
        if (strlen(t->strings[i]) == len && strncmp(t->strings[i], start, len) == 0) {
            return (uint16_t)i;
        }
    }
    if (t->count >= t->capacity) {
        t->capacity *= 2;
        t->strings = realloc(t->strings, t->capacity * sizeof(char*));
    }
    char* copy = malloc(len + 1);
    memcpy(copy, start, len);
    copy[len] = '\0';
    t->strings[t->count] = copy;
    return (uint16_t)t->count++;
}

// --- Struct Registry ---

typedef struct {
    char* name;
    Buffer bytecode;
} StructDef;

typedef struct {
    StructDef* defs;
    size_t count;
    size_t capacity;
} StructRegistry;

void reg_init(StructRegistry* r) {
    r->count = 0;
    r->capacity = 8;
    r->defs = malloc(r->capacity * sizeof(StructDef));
}

StructDef* reg_add(StructRegistry* r, const char* name, int len) {
    if (r->count >= r->capacity) {
        r->capacity *= 2;
        r->defs = realloc(r->defs, r->capacity * sizeof(StructDef));
    }
    StructDef* def = &r->defs[r->count++];
    def->name = malloc(len + 1);
    memcpy(def->name, name, len);
    def->name[len] = '\0';
    buf_init(&def->bytecode);
    return def;
}

StructDef* reg_find(StructRegistry* r, const char* name, int len) {
    for (size_t i = 0; i < r->count; i++) {
        if (strlen(r->defs[i].name) == len && strncmp(r->defs[i].name, name, len) == 0) {
            return &r->defs[i];
        }
    }
    return NULL;
}

// --- Lexer ---

void lexer_init(Lexer* lexer, const char* source) {
    lexer->source = source;
    lexer->current = source;
    lexer->line = 1;
}

int is_alpha_c(char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_'; }
int is_digit_c(char c) { return c >= '0' && c <= '9'; }

Token lexer_next(Lexer* lexer) {
    while (*lexer->current != '\0') {
        char c = *lexer->current;

        if (c == ' ' || c == '\r' || c == '\t') { lexer->current++; continue; }
        if (c == '\n') { lexer->line++; lexer->current++; continue; }
        if (c == '/' && *(lexer->current + 1) == '/') {
            while (*lexer->current != '\0' && *lexer->current != '\n') lexer->current++;
            continue;
        }

        lexer->current++;
        Token token;
        token.start = lexer->current - 1;
        token.line = lexer->line;
        token.length = 1;

        switch (c) {
            case '{': token.type = TOK_LBRACE; return token;
            case '}': token.type = TOK_RBRACE; return token;
            case '[': token.type = TOK_LBRACKET; return token;
            case ']': token.type = TOK_RBRACKET; return token;
            case '(': token.type = TOK_LPAREN; return token;
            case ')': token.type = TOK_RPAREN; return token;
            case ';': token.type = TOK_SEMICOLON; return token;
            case ':': token.type = TOK_COLON; return token;
            case '@': token.type = TOK_AT; return token;
            case '"': {
                token.type = TOK_STRING;
                token.start = lexer->current;
                while (*lexer->current != '"' && *lexer->current != '\0') lexer->current++;
                token.length = (int)(lexer->current - token.start);
                if (*lexer->current == '"') lexer->current++;
                return token;
            }
        }

        if (is_alpha_c(c)) {
            token.type = TOK_IDENTIFIER;
            while (is_alpha_c(*lexer->current) || is_digit_c(*lexer->current)) {
                lexer->current++;
            }
            token.length = (int)(lexer->current - token.start);
            return token;
        }

        if (is_digit_c(c)) {
            token.type = TOK_NUMBER;
            if (c == '0' && (*lexer->current == 'x' || *lexer->current == 'X')) {
                lexer->current++;
                while (isxdigit(*lexer->current)) lexer->current++;
            } else {
                while (is_digit_c(*lexer->current)) lexer->current++;
            }
            token.length = (int)(lexer->current - token.start);
            return token;
        }
        
        printf("Lexer Error: Unexpected char '%c' line %d\n", c, lexer->line);
        exit(1);
    }
    Token t = { TOK_EOF, lexer->current, 0, lexer->line };
    return t;
}

// --- Parser ---

typedef struct {
    Lexer lexer;
    Token current;
    Token previous;
    Buffer* target;     // Current buffer being emitted to (Global or Struct)
    Buffer global_bc;   // Top-level packet bytecode
    StringTable strtab;
    StructRegistry registry;
    int had_error;
} Parser;

void parser_error(Parser* p, const char* msg) {
    printf("Error at line %d: %s (Token: '%.*s')\n", 
        p->current.line, msg, p->current.length, p->current.start);
    p->had_error = 1;
}

void advance(Parser* p) {
    p->previous = p->current;
    p->current = lexer_next(&p->lexer);
}

void consume(Parser* p, TokenType type, const char* msg) {
    if (p->current.type == type) {
        advance(p);
        return;
    }
    parser_error(p, msg);
}

int match_keyword(Token t, const char* kw) {
    if (t.type != TOK_IDENTIFIER) return 0;
    if (strlen(kw) != t.length) return 0;
    return strncmp(t.start, kw, t.length) == 0;
}

uint32_t parse_number(Token t) {
    char buf[64];
    if (t.length >= 63) return 0;
    memcpy(buf, t.start, t.length);
    buf[t.length] = '\0';
    return (uint32_t)strtoul(buf, NULL, 0); 
}

// --- Parsing Logic ---

void parse_field(Parser* p) {
    if (p->had_error) return;

    // Decorators
    uint32_t array_fixed_count = 0; // Fixed array size from [N]
    int has_fixed_array_count = 0; // Flag for fixed array
    uint32_t const_val = 0;
    int has_const = 0;
    int is_big_endian_field = 0; // Flag for @big_endian field decorator
    
    while (p->current.type == TOK_AT) {
        advance(p); 
        Token dec_name = p->current;
        consume(p, TOK_IDENTIFIER, "Expect decorator name");
        
        // Handle decorators without arguments
        if (match_keyword(dec_name, "big_endian")) {
            is_big_endian_field = 1;
            // No arguments, so just advance past decorator name
        } else if (match_keyword(dec_name, "fill")) {
            // @fill takes no arguments, it just aligns to the next byte
            buf_push(p->target, OP_ALIGN_FILL);
        } else {
             // Decorators with arguments
            consume(p, TOK_LPAREN, "Expect ("); 
            if (match_keyword(dec_name, "count")) {
                Token num = p->current;
                consume(p, TOK_NUMBER, "Expect count number");
                array_fixed_count = parse_number(num);
                has_fixed_array_count = 1;
            } else if (match_keyword(dec_name, "const")) {
                Token num = p->current;
                consume(p, TOK_NUMBER, "Expect const value");
                const_val = parse_number(num);
                has_const = 1;
            } else if (match_keyword(dec_name, "pad")) {
                Token num = p->current;
                consume(p, TOK_NUMBER, "Expect pad bits");
                uint32_t pad_bits = parse_number(num);
                buf_push(p->target, OP_ALIGN_PAD);
                buf_push(p->target, (uint8_t)pad_bits);
            } else {
                while (p->current.type != TOK_RPAREN && p->current.type != TOK_EOF) advance(p);
            }
            consume(p, TOK_RPAREN, "Expect )");
        }
    }

    // 1. Parse Type (e.g., uint8 or string)
    Token type_tok = p->current;
    consume(p, TOK_IDENTIFIER, "Expect field type");

    // 2. Parse Name
    Token name_tok = p->current;
    consume(p, TOK_IDENTIFIER, "Expect field name");
    uint16_t key_id = strtab_add(&p->strtab, name_tok.start, name_tok.length);

    uint8_t bit_width = 0;
    if (p->current.type == TOK_COLON) {
        advance(p); // consume ':'
        Token bits = p->current;
        consume(p, TOK_NUMBER, "Expect bit width");
        bit_width = (uint8_t)parse_number(bits);
    }

    // 3. Check for Array suffix (e.g., uint8[] or uint8[3]) and related prefix/until/max modifiers
    int is_array_field = 0;
    int is_variable_array = 0; // Flag for [] (variable) vs [N] (fixed)
    uint8_t arr_prefix_op = OP_NOOP;
    uint8_t str_prefix_op = OP_NOOP;
    uint16_t max_len = 255; // Default max_len for STR_NULL if not specified
    
    // Check for array suffix
    if (p->current.type == TOK_LBRACKET) {
        advance(p); // consume '['
        is_array_field = 1;

        if (p->current.type == TOK_RBRACKET) { // empty [] for variable length
            advance(p); // consume ']'
            is_variable_array = 1;
        } else { // [N] for fixed length
            Token num = p->current;
            consume(p, TOK_NUMBER, "Expect array size");
            array_fixed_count = parse_number(num);
            has_fixed_array_count = 1;
            consume(p, TOK_RBRACKET, "Expect ]"); // consume ']'
        }
    }
    
    // Check for 'prefix' keyword (for variable arrays or strings) or 'until/max' for strings
    if (match_keyword(p->current, "prefix")) {
        advance(p); // consume 'prefix'
        Token ptype = p->current;
        consume(p, TOK_IDENTIFIER, "Expect prefix type"); // Consumes prefix type
        
        if (is_variable_array) { // Variable Array prefix
            if (match_keyword(ptype, "uint8") || match_keyword(ptype, "u8")) arr_prefix_op = OP_ARR_PRE_U8;
            else if (match_keyword(ptype, "uint16") || match_keyword(ptype, "u16")) arr_prefix_op = OP_ARR_PRE_U16;
            else if (match_keyword(ptype, "uint32") || match_keyword(ptype, "u32")) arr_prefix_op = OP_ARR_PRE_U32;
            else parser_error(p, "Invalid prefix type for variable array");
        } else if (match_keyword(type_tok, "string")) { // String prefix
            if (match_keyword(ptype, "uint8") || match_keyword(ptype, "u8")) str_prefix_op = OP_STR_PRE_U8;
            else if (match_keyword(ptype, "uint16") || match_keyword(ptype, "u16")) str_prefix_op = OP_STR_PRE_U16;
            else if (match_keyword(ptype, "uint32") || match_keyword(ptype, "u32")) str_prefix_op = OP_STR_PRE_U32;
            else parser_error(p, "Invalid prefix type for string");
        } else {
            parser_error(p, "Prefix keyword used for non-variable-array/non-string type");
        }
    } else if (match_keyword(type_tok, "string")) { // Check for 'until' or 'max' only if it's a string and no prefix
        if (match_keyword(p->current, "until")) {
            advance(p); consume(p, TOK_NUMBER, "Expect val"); // For now, ignoring value. Assuming 0x00
        }
        if (match_keyword(p->current, "max")) {
            advance(p);
            Token max_tok = p->current;
            consume(p, TOK_NUMBER, "Expect max length");
            max_len = (uint16_t)parse_number(max_tok);
        }
    }

    // Emit Endianness for this field if specified
    if (is_big_endian_field) {
        buf_push(p->target, OP_SET_ENDIAN_BE);
    }


    // Emit Array / String prefix Opcodes (these emit BEFORE the actual field)
    if (arr_prefix_op != OP_NOOP) {
        buf_push(p->target, arr_prefix_op);
        buf_push_u16(p->target, key_id); // KeyID for the count field
    } else if (has_fixed_array_count && is_array_field) { // has_count from decorator or fixed [N]
        buf_push(p->target, OP_ARR_FIXED);
        buf_push_u16(p->target, key_id); // KeyID for the array
        buf_push_u16(p->target, (uint16_t)array_fixed_count);
    } else if (str_prefix_op != OP_NOOP) {
        buf_push(p->target, str_prefix_op);
        buf_push_u16(p->target, key_id);
    }

    // Handle @const (Const has higher precedence for emission, but applied to the underlying type)
    if (has_const) {
        uint8_t type_op = OP_IO_U16; // Default to U16
        if (match_keyword(type_tok, "uint8") || match_keyword(type_tok, "byte")) type_op = OP_IO_U8;
        else if (match_keyword(type_tok, "uint32") || match_keyword(type_tok, "u32")) type_op = OP_IO_U32;
        else if (match_keyword(type_tok, "uint64") || match_keyword(type_tok, "u64")) type_op = OP_IO_U64;
        
        buf_push(p->target, OP_CONST_CHECK);
        buf_push(p->target, type_op);
        if (type_op == OP_IO_U8) buf_push(p->target, (uint8_t)const_val);
        else if (type_op == OP_IO_U16) buf_push_u16(p->target, (uint16_t)const_val);
        else if (type_op == OP_IO_U32) buf_push_u32(p->target, const_val);
        else parser_error(p, "Const type not yet supported for u64"); // Simple consts for now
    } else {
        // Standard Field emission
        StructDef* sdef = reg_find(&p->registry, type_tok.start, type_tok.length);
        
        if (sdef) {
            buf_push(p->target, OP_ENTER_STRUCT);
            buf_push_u16(p->target, key_id);
            // printf("COMPILER_DEBUG: Appending struct '%s' bytecode (size %zu) to target (current size %zu)\n", sdef->name, sdef->bytecode.size, p->target->size);
            buf_append(p->target, sdef->bytecode.data, sdef->bytecode.size);
            // printf("COMPILER_DEBUG: After append, target size %zu\n", p->target->size);
            buf_push(p->target, OP_EXIT_STRUCT);
        } 
        else if (match_keyword(type_tok, "string")) {
            // Only emit OP_STR_NULL if no prefix (prefix already handled above)
            if (str_prefix_op == OP_NOOP) { 
                // String properties (until, max) need to be handled here.
                // Re-parsing this seems duplicative, the state should have been captured.
                // For now, these are ignored if string is prefix type.
                buf_push(p->target, OP_STR_NULL);
                buf_push_u16(p->target, key_id);
                buf_push_u16(p->target, max_len); // Use default max_len 255 if not specified
            }
        } else {
            // Primitive
            uint8_t op = OP_NOOP;
            if (bit_width > 0) {
                 if (match_keyword(type_tok, "uint8") || match_keyword(type_tok, "byte") ||
                     match_keyword(type_tok, "uint16") || match_keyword(type_tok, "u16") ||
                     match_keyword(type_tok, "uint32") || match_keyword(type_tok, "u32") ||
                     match_keyword(type_tok, "uint64") || match_keyword(type_tok, "u64")) {
                     op = OP_IO_BIT_U;
                 }
                 else if (match_keyword(type_tok, "int8") || match_keyword(type_tok, "i8") ||
                          match_keyword(type_tok, "int16") || match_keyword(type_tok, "i16") ||
                          match_keyword(type_tok, "int32") || match_keyword(type_tok, "i32") ||
                          match_keyword(type_tok, "int64") || match_keyword(type_tok, "i64")) {
                     op = OP_IO_BIT_I;
                 }
                 else {
                     parser_error(p, "Bitfields only supported for integer types");
                     return;
                 }
                 
                 buf_push(p->target, op);
                 buf_push_u16(p->target, key_id);
                 buf_push(p->target, bit_width);
            } else {
                if (match_keyword(type_tok, "uint8") || match_keyword(type_tok, "byte")) op = OP_IO_U8;
                else if (match_keyword(type_tok, "uint16") || match_keyword(type_tok, "u16")) op = OP_IO_U16;
                else if (match_keyword(type_tok, "uint32") || match_keyword(type_tok, "u32")) op = OP_IO_U32;
                else if (match_keyword(type_tok, "uint64") || match_keyword(type_tok, "u64")) op = OP_IO_U64;
                else if (match_keyword(type_tok, "int8") || match_keyword(type_tok, "i8")) op = OP_IO_I8;
                else if (match_keyword(type_tok, "int16") || match_keyword(type_tok, "i16")) op = OP_IO_I16;
                else if (match_keyword(type_tok, "int32") || match_keyword(type_tok, "i32")) op = OP_IO_I32;
                else if (match_keyword(type_tok, "int64") || match_keyword(type_tok, "i64")) op = OP_IO_I64;
                else if (match_keyword(type_tok, "float") || match_keyword(type_tok, "f32")) op = OP_IO_F32;
                else if (match_keyword(type_tok, "double") || match_keyword(type_tok, "f64")) op = OP_IO_F64;
                else {
                    parser_error(p, "Unknown type");
                    return;
                }
                buf_push(p->target, op);
                buf_push_u16(p->target, key_id);
            }
        }
    }
    
    // An array field (fixed or prefixed) needs an ARR_END.
    if (is_array_field) { 
        buf_push(p->target, OP_ARR_END);
    }

    consume(p, TOK_SEMICOLON, "Expect ; after field");

    // After emitting field ops, if endianness was set, reset it to default LE
    if (is_big_endian_field) {
        buf_push(p->target, OP_SET_ENDIAN_LE); // Reset to LE after field
    }
}

void parse_block(Parser* p) {
    consume(p, TOK_LBRACE, "Expect {");
    while (p->current.type != TOK_RBRACE && p->current.type != TOK_EOF && !p->had_error) {
        parse_field(p);
    }
    consume(p, TOK_RBRACE, "Expect }");
}

void parse_struct(Parser* p) {
    Token name = p->current;
    consume(p, TOK_IDENTIFIER, "Expect struct name");
    StructDef* def = reg_add(&p->registry, name.start, name.length);
    
    Buffer* prev = p->target;
    p->target = &def->bytecode;
    parse_block(p);
    p->target = prev;
}

void parse_packet(Parser* p) {
    Token name = p->current;
    consume(p, TOK_IDENTIFIER, "Expect packet name");
    // Packet writes to global buffer
    parse_block(p);
}

void parse_top_level(Parser* p) {
    while (p->current.type != TOK_EOF && !p->had_error) {
        if (p->current.type == TOK_AT) {
            advance(p); 
            Token dec = p->current;
            consume(p, TOK_IDENTIFIER, "Expect decorator name");
            
            // Handle global decorators without arguments
            if (match_keyword(dec, "version")) { // Version has args
                consume(p, TOK_LPAREN, "Expect (");
                Token ver = p->current;
                consume(p, TOK_NUMBER, "Expect version number");
                uint32_t v = parse_number(ver);
                buf_push(p->target, OP_META_VERSION);
                buf_push(p->target, (uint8_t)v);
                consume(p, TOK_RPAREN, "Expect )");
            } else if (match_keyword(dec, "big_endian")) { // Global big_endian without args
                buf_push(p->target, OP_SET_ENDIAN_BE);
            } else if (match_keyword(dec, "little_endian") || match_keyword(dec, "le")) { // Global little_endian without args
                buf_push(p->target, OP_SET_ENDIAN_LE);
            } else {
                // Unknown decorator, skip until ')' if has args, or until next token.
                // For now, assume it's a decorator with args we don't know, so skip ( ... )
                if (p->current.type == TOK_LPAREN) {
                    consume(p, TOK_LPAREN, "Expect (");
                    while (p->current.type != TOK_RPAREN && p->current.type != TOK_EOF) advance(p);
                    consume(p, TOK_RPAREN, "Expect )");
                }
            }
        } else if (match_keyword(p->current, "struct")) {
            advance(p);
            parse_struct(p);
        } else if (match_keyword(p->current, "packet")) {
            advance(p);
            parse_packet(p);
        } else {
            parser_error(p, "Unexpected token");
        }
    }
}

// --- API Implementation ---

int cnd_compile_file(const char* in_path, const char* out_path) {
    setbuf(stdout, NULL); // Ensure debug prints are flushed immediately
    FILE* f = fopen(in_path, "rb");
    if (!f) { printf("Error opening input file: %s\n", in_path); return 1; }
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    char* source = malloc(size + 1);
    fread(source, 1, size, f);
    source[size] = '\0';
    fclose(f);

    Parser p;
    lexer_init(&p.lexer, source);
    buf_init(&p.global_bc);
    strtab_init(&p.strtab);
    reg_init(&p.registry);
    p.had_error = 0;
    p.target = &p.global_bc;
    
    advance(&p); 
    parse_top_level(&p);
    
    if (p.had_error) {
        free(source);
        buf_free(&p.global_bc);
        return 1;
    }

    FILE* out = fopen(out_path, "wb");
    if (!out) { printf("Error opening output file: %s\n", out_path); return 1; }

    fwrite("CNDIL", 1, 5, out);
    fputc(1, out);
    uint16_t str_count = (uint16_t)p.strtab.count;
    fwrite(&str_count, 2, 1, out); 
    
    uint32_t str_offset = 16;
    uint32_t str_bytes = 0;
    for(size_t i=0; i<p.strtab.count; i++) {
        str_bytes += strlen(p.strtab.strings[i]) + 1;
    }
    uint32_t bytecode_offset = str_offset + str_bytes;
    
    fwrite(&str_offset, 4, 1, out);
    fwrite(&bytecode_offset, 4, 1, out);
    
    for(size_t i=0; i<p.strtab.count; i++) {
        fwrite(p.strtab.strings[i], 1, strlen(p.strtab.strings[i]) + 1, out);
    }
    
    fwrite(p.global_bc.data, 1, p.global_bc.size, out);
    
    fclose(out);
    printf("Successfully compiled %s to %s\n", in_path, out_path);
    printf("  Strings: %zu\n", p.strtab.count);
    printf("  Bytecode: %zu bytes\n", p.global_bc.size);

    free(source);
    buf_free(&p.global_bc);
    return 0;
}
