#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <inttypes.h>
#include <cJSON.h>
#include "../compiler/cnd_internal.h"
#include "cli_helpers.h"
#include "compiler.h" // For cnd_format_source

#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#endif

// --- Built-in Decorators ---
typedef struct {
    const char* name;
    const char* doc;
    const char* detail;
} BuiltinDecorator;

static const BuiltinDecorator BUILTIN_DECORATORS[] = {
    {"version", "Sets the version of the schema.", "version(1)"},
    {"import", "Imports another CND file.", "import(\"path/to/file.cnd\")"},
    {"big_endian", "Sets the byte order to Big Endian for the following fields.", "big_endian"},
    {"be", "Alias for @big_endian.", "be"},
    {"little_endian", "Sets the byte order to Little Endian for the following fields.", "little_endian"},
    {"le", "Alias for @little_endian.", "le"},
    {"unaligned_bytes", "Marks a struct as containing unaligned bitfields.", "unaligned_bytes"},
    {"fill", "Inserts padding bits/bytes. Can be used as a standalone statement.", "fill(1) or fill(0)"},
    {"crc_refin", "Sets CRC input reflection.", "crc_refin"},
    {"crc_refout", "Sets CRC output reflection.", "crc_refout"},
    {"optional", "Marks a field as optional (implementation specific).", "optional"},
    {"count", "Sets the count for an array (fixed number or variable reference).", "count(N) or count(field_name)"},
    {"len", "Alias for @count.", "len(N) or len(field_name)"},
    {"const", "Enforces a constant value for a field.", "const(VALUE)"},
    {"match", "Alias for @const.", "match(VALUE)"},
    {"pad", "Inserts padding bits.", "pad(BITS)"},
    {"range", "Enforces a value range.", "range(MIN, MAX)"},
    {"crc", "Calculates CRC over previous fields.", "crc(WIDTH)"},
    {"crc_poly", "Sets CRC polynomial.", "crc_poly(POLY)"},
    {"crc_init", "Sets CRC initial value.", "crc_init(VAL)"},
    {"crc_xor", "Sets CRC XOR value.", "crc_xor(VAL)"},
    {"scale", "Applies linear scaling (y = x * scale + offset).", "scale(FACTOR)"},
    {"offset", "Applies offset for scaling.", "offset(VAL)"},
    {"mul", "Multiplies value by factor.", "mul(FACTOR)"},
    {"div", "Divides value by factor.", "div(FACTOR)"},
    {"add", "Adds value.", "add(VAL)"},
    {"sub", "Subtracts value.", "sub(VAL)"},
    {"poly", "Applies polynomial transformation.", "poly(c0, c1, ...)"},
    {"spline", "Applies spline transformation.", "spline(x0, y0, x1, y1, ...)"},
    {"expr", "Calculates a value based on an expression.", "expr(expression)"},
    {"eof", "Marks a byte array to consume all remaining bytes in the stream.", "eof"},
    {NULL, NULL, NULL}
};

// --- LSP Protocol Helpers ---

static void send_json(cJSON* json) {
    char* str = cJSON_PrintUnformatted(json);
    if (!str) return;
    size_t len = strlen(str);
    // Use \r\n for headers as per spec
    fprintf(stdout, "Content-Length: %zu\r\n\r\n%s", len, str);
    fflush(stdout);
    free(str);
}

static cJSON* read_json(void) {
    // Read header: "Content-Length: <N>\r\n\r\n"
    size_t content_len = 0;
    while (1) {
        char line[1024];
        // fgets reads until newline. In binary mode, \r\n is preserved.
        if (!fgets(line, sizeof(line), stdin)) return NULL;
        
        if (strncmp(line, "Content-Length: ", 16) == 0) {
            content_len = atoi(line + 16);
        } else if (strcmp(line, "\r\n") == 0 || strcmp(line, "\n") == 0) { 
            // Handle both \r\n and \n just in case, though spec says \r\n
            break; // End of headers
        }
    }
    
    if (content_len == 0) return NULL;
    
    char* buf = malloc(content_len + 1);
    if (!buf) return NULL;
    size_t read_len = fread(buf, 1, content_len, stdin);
    buf[read_len] = 0;
    
    cJSON* json = cJSON_Parse(buf);
    free(buf);
    return json;
}

static void send_response(cJSON* id, cJSON* result) {
    cJSON* res = cJSON_CreateObject();
    cJSON_AddStringToObject(res, "jsonrpc", "2.0");
    if (id) cJSON_AddItemToObject(res, "id", cJSON_Duplicate(id, 1));
    cJSON_AddItemToObject(res, "result", result);
    send_json(res);
    cJSON_Delete(res);
}

// --- Document Store ---
typedef struct {
    char* uri;
    char* content;
} LspDocument;

static LspDocument* docs = NULL;
static size_t doc_count = 0;
static size_t doc_cap = 0;

static void doc_update(const char* uri, const char* content) {
    for (size_t i = 0; i < doc_count; i++) {
        if (strcmp(docs[i].uri, uri) == 0) {
            free(docs[i].content);
            docs[i].content = strdup(content);
            return;
        }
    }
    if (doc_count >= doc_cap) {
        doc_cap = (doc_cap == 0) ? 8 : doc_cap * 2;
        docs = realloc(docs, doc_cap * sizeof(LspDocument));
    }
    docs[doc_count].uri = strdup(uri);
    docs[doc_count].content = strdup(content);
    doc_count++;
}

static char* doc_get(const char* uri) {
    for (size_t i = 0; i < doc_count; i++) {
        if (strcmp(docs[i].uri, uri) == 0) {
            return strdup(docs[i].content); // Return copy
        }
    }
    return NULL;
}

#ifdef __GNUC__
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#endif

static void doc_remove(const char* uri) {
    (void)uri;
    /*
    for (size_t i = 0; i < doc_count; i++) {
        if (strcmp(docs[i].uri, uri) == 0) {
            free(docs[i].uri);
            free(docs[i].content);
            if (i < doc_count - 1) {
                docs[i] = docs[doc_count - 1];
            }
            doc_count--;
            return;
        }
    }
    */
}

static void handle_didClose(cJSON* params) {
    (void)params; // Unused
    /*
    cJSON* doc = cJSON_GetObjectItem(params, "textDocument");
    if (!doc) return;
    cJSON* uri = cJSON_GetObjectItem(doc, "uri");
    if (uri && uri->valuestring) {
        doc_remove(uri->valuestring);
    }
    */
}

#ifdef __GNUC__
#pragma GCC diagnostic pop
#endif

// --- Analysis Logic ---

static const char* get_type_name(uint8_t type) {
    switch (type) {
        case OP_IO_U8: return "u8";
        case OP_IO_U16: return "u16";
        case OP_IO_U32: return "u32";
        case OP_IO_U64: return "u64";
        case OP_IO_I8: return "i8";
        case OP_IO_I16: return "i16";
        case OP_IO_I32: return "i32";
        case OP_IO_I64: return "i64";
        case OP_IO_BIT_U: return "bit_u";
        case OP_IO_BIT_I: return "bit_i";
        case OP_IO_BIT_BOOL: return "bit_bool";
        default: return "unknown";
    }
}

// Helper to analyze a source string and find definition for symbol at position
typedef struct {
    int found;
    int def_line;
    char* def_file;
    char* symbol_name;
    char* doc_comment;
    char* type_details;
} AnalysisResult;

// We need to parse the file to build the registry.
// But we also need to find what symbol is under the cursor.
// We can re-tokenize the source to find the token at (line, char).
// Then look up that token in the registry.

static void analyze_source(const char* source, const char* file_path, int line, int character, AnalysisResult* res) {
    res->found = 0;
    res->def_line = 0;
    res->def_file = NULL;
    res->symbol_name = NULL;
    res->doc_comment = NULL;
    res->type_details = NULL;

    // 1. Setup Parser to build registry
    Parser p;
    memset(&p, 0, sizeof(Parser));
    p.json_output = 0; 
    p.silent = 1; // CRITICAL: Suppress stdout/stderr to avoid breaking LSP
    
    // Initialize buffers
    Buffer bc;
    buf_init(&bc); p.target = &bc;
    buf_init(&p.global_bc);
    strtab_init(&p.strtab);
    strtab_init(&p.imports);
    reg_init(&p.registry);
    enum_reg_init(&p.enums);
    
    lexer_init(&p.lexer, source);
    p.current_path = file_path; // For import resolution (might fail for relative imports if not handled) 
    
    advance(&p);
    parse_top_level(&p);
    
    // Registry is now populated. 
    
    // 2. Scan tokens again to find the cursor token
    Lexer scanner;
    lexer_init(&scanner, source);
    
    Token target_token = {TOK_EOF, NULL, 0, 0};
    int target_is_decorator = 0;
    Token prev_token = {TOK_EOF, NULL, 0, 0};
    
    for (;;) {
        Token t = lexer_next(&scanner);
        if (t.type == TOK_EOF) break;
        
        // Check intersection
        // LSP lines are 0-based. Token lines are 1-based (usually).
        // Let's assume lexer uses 1-based.
        int tok_line = t.line - 1; 
        
        // Calculate column
        // We need start of line pointer. 
        // Lexer doesn't store it in Token easily without backtracking.
        // But we can assume basic matching:
        // If line matches...
        if (tok_line == line) {
            // Check column. 
            // t.start is pointer into source.
            // We need to calculate offset from line start.
            const char* line_start = t.start;
            while (line_start > source && *(line_start-1) != '\n') line_start--;
            int col_start = (int)(t.start - line_start);
            int col_end = col_start + t.length;
            
            if (character >= col_start && character <= col_end) {
                target_token = t;
                if (prev_token.type == TOK_AT) target_is_decorator = 1;
                break;
            }
        }
        if (tok_line > line) break; 
        prev_token = t;
    }
    
    if (target_token.type == TOK_IDENTIFIER) {
        if (target_is_decorator) {
             for (int i = 0; BUILTIN_DECORATORS[i].name; i++) {
                 if (strlen(BUILTIN_DECORATORS[i].name) == (size_t)target_token.length &&
                     strncmp(BUILTIN_DECORATORS[i].name, target_token.start, target_token.length) == 0) {
                     res->found = 1;
                     res->symbol_name = strdup(BUILTIN_DECORATORS[i].name);
                     res->doc_comment = strdup(BUILTIN_DECORATORS[i].doc);
                     res->type_details = strdup(BUILTIN_DECORATORS[i].detail);
                     res->def_file = strdup("built-in");
                     res->def_line = 0;
                     goto cleanup;
                 }
             }
        }

        // Look up in registry
        StructDef* sdef = reg_find(&p.registry, target_token.start, target_token.length);
        if (sdef) {
            res->found = 1;
            res->def_line = sdef->line - 1; // Convert to 0-based
            if (sdef->file) res->def_file = strdup(sdef->file);
            if (sdef->doc_comment) res->doc_comment = strdup(sdef->doc_comment);
            res->symbol_name = malloc(target_token.length + 1);
            if (res->symbol_name) {
                memcpy(res->symbol_name, target_token.start, target_token.length);
                res->symbol_name[target_token.length] = '\0';
            }
        } else {
            EnumDef* edef = enum_reg_find(&p.enums, target_token.start, target_token.length);
            if (edef) {
                res->found = 1;
                res->def_line = edef->line - 1;
                if (edef->file) res->def_file = strdup(edef->file);
                if (edef->doc_comment) res->doc_comment = strdup(edef->doc_comment);
                res->symbol_name = malloc(target_token.length + 1);
                if (res->symbol_name) {
                    memcpy(res->symbol_name, target_token.start, target_token.length);
                    res->symbol_name[target_token.length] = '\0';
                }

                // Generate type details
                Buffer tb; buf_init(&tb);
                char tmp[256];
                snprintf(tmp, sizeof(tmp), "Type: `%s`\n\nMembers:\n", get_type_name(edef->underlying_type));
                buf_append(&tb, (uint8_t*)tmp, strlen(tmp));
                
                for(size_t i=0; i<edef->count; i++) {
                    snprintf(tmp, sizeof(tmp), "- `%s` = `%" PRId64 "`\n", edef->values[i].name, edef->values[i].value);
                    buf_append(&tb, (uint8_t*)tmp, strlen(tmp));
                }
                buf_push(&tb, 0);
                res->type_details = strdup((char*)tb.data);
                buf_free(&tb);
            }
        }
    }
    
    // Cleanup
cleanup:
    buf_free(&bc);
    buf_free(&p.global_bc);
    strtab_free(&p.strtab);
    strtab_free(&p.imports);
    reg_free(&p.registry);
    enum_reg_free(&p.enums);
    if (p.errors) {
        for (int i = 0; i < p.error_count; i++) free(p.errors[i].message);
        free(p.errors);
    }
}

// --- Handlers ---

// Helper for URI decoding
static void url_decode(const char* src, char* dst) {
    char a, b;
    while (*src) {
        if ((*src == '%') &&
            ((a = src[1]) && (b = src[2])) &&
            (isxdigit(a) && isxdigit(b))) {
            if (a >= 'a') a -= 'a'-'A';
            if (a >= 'A') a -= ('A' - 10);
            else a -= '0';
            if (b >= 'a') b -= 'a'-'A';
            if (b >= 'A') b -= ('A' - 10);
            else b -= '0';
            *dst++ = 16*a+b;
            src+=3;
        } else {
            *dst++ = *src++;
        }
    }
    *dst++ = '\0';
}

static char* file_uri_to_path(const char* uri) {
    // Skip "file://"
    const char* raw_path = uri;
    if (strncmp(uri, "file://", 7) == 0) {
        raw_path = uri + 7;
    }
    
    char* decoded = malloc(strlen(raw_path) + 1);
    url_decode(raw_path, decoded);
    
    // Handle Windows "/C:/..." -> "C:/..."
    // Also handle lower case drive letter normalization if needed?
    if (decoded[0] == '/' && isalpha(decoded[1]) && decoded[2] == ':') {
        char* fixed = strdup(decoded + 1);
        free(decoded);
        return fixed;
    }
    
    return decoded;
}

// --- Bytecode Helpers for Completion ---
static uint8_t read_u8(const uint8_t** ptr, const uint8_t* end) {
    if (*ptr >= end) return 0;
    return *(*ptr)++;
}

static uint16_t read_u16(const uint8_t** ptr, const uint8_t* end) {
    if (*ptr + 2 > end) return 0;
    uint16_t v = (*ptr)[0] | ((*ptr)[1] << 8);
    *ptr += 2;
    return v;
}

static void skip_instruction(const uint8_t** ptr, const uint8_t* end, uint8_t op) {
    switch (op) {
        case OP_META_VERSION: *ptr += 1; break;
        case OP_META_NAME: *ptr += 2; break;
        case OP_IO_U8: case OP_IO_U16: case OP_IO_U32: case OP_IO_U64:
        case OP_IO_I8: case OP_IO_I16: case OP_IO_I32: case OP_IO_I64:
        case OP_IO_F32: case OP_IO_F64: case OP_IO_BOOL:
        case OP_ENTER_STRUCT: *ptr += 2; break;
        case OP_STR_NULL: *ptr += 4; break;
        case OP_STR_PRE_U8: case OP_STR_PRE_U16: case OP_STR_PRE_U32:
        case OP_ARR_PRE_U8: case OP_ARR_PRE_U16: case OP_ARR_PRE_U32: *ptr += 2; break;
        case OP_IO_BIT_U: case OP_IO_BIT_I: *ptr += 3; break;
        case OP_IO_BIT_BOOL: *ptr += 2; break;
        case OP_ARR_FIXED: *ptr += 6; break;
        case OP_RAW_BYTES: *ptr += 6; break;
        case OP_CONST_WRITE: {
            uint8_t type = read_u8(ptr, end);
            if (type == OP_IO_U8) *ptr += 1;
            else if (type == OP_IO_U16) *ptr += 2;
            else if (type == OP_IO_U32) *ptr += 4;
            else if (type == OP_IO_U64) *ptr += 8;
            break;
        }
        case OP_CONST_CHECK: {
            *ptr += 2; // key
            uint8_t type = read_u8(ptr, end);
            if (type == OP_IO_U8 || type == OP_IO_I8) *ptr += 1;
            else if (type == OP_IO_U16 || type == OP_IO_I16) *ptr += 2;
            else if (type == OP_IO_U32 || type == OP_IO_I32) *ptr += 4;
            else if (type == OP_IO_U64 || type == OP_IO_I64) *ptr += 8;
            break;
        }
        case OP_RANGE_CHECK: {
            uint8_t type = read_u8(ptr, end);
            if (type == OP_IO_U8 || type == OP_IO_I8) *ptr += 2;
            else if (type == OP_IO_U16 || type == OP_IO_I16) *ptr += 4;
            else if (type == OP_IO_U32 || type == OP_IO_I32 || type == OP_IO_F32) *ptr += 8;
            else if (type == OP_IO_U64 || type == OP_IO_I64 || type == OP_IO_F64) *ptr += 16;
            break;
        }
        case OP_SCALE_LIN: *ptr += 16; break;
        case OP_TRANS_ADD: case OP_TRANS_SUB: case OP_TRANS_MUL: case OP_TRANS_DIV: *ptr += 8; break;
        case OP_TRANS_POLY: {
            uint8_t count = read_u8(ptr, end);
            *ptr += count * 8;
            break;
        }
        case OP_TRANS_SPLINE: {
            uint8_t count = read_u8(ptr, end);
            *ptr += count * 16;
            break;
        }
        case OP_CRC_16: *ptr += 7; break;
        case OP_CRC_32: *ptr += 13; break;
        case OP_ENUM_CHECK: {
            uint8_t type = read_u8(ptr, end);
            uint16_t count = read_u16(ptr, end);
            int sz = 0;
            if (type == OP_IO_U8 || type == OP_IO_I8) sz = 1;
            else if (type == OP_IO_U16 || type == OP_IO_I16) sz = 2;
            else if (type == OP_IO_U32 || type == OP_IO_I32) sz = 4;
            else if (type == OP_IO_U64 || type == OP_IO_I64) sz = 8;
            *ptr += count * sz;
            break;
        }
        case OP_SWITCH: *ptr += 6; break;
        case OP_JUMP: case OP_JUMP_IF_NOT: *ptr += 4; break;
        case OP_LOAD_CTX: case OP_STORE_CTX: *ptr += 2; break;
        case OP_PUSH_IMM: *ptr += 8; break;
        case OP_ALIGN_PAD: case OP_ALIGN_FILL: *ptr += 1; break;
        default: break;
    }
}

static void handle_completion(cJSON* id, cJSON* params) {
    cJSON* doc = cJSON_GetObjectItem(params, "textDocument");
    cJSON* uri_item = cJSON_GetObjectItem(doc, "uri");
    cJSON* pos = cJSON_GetObjectItem(params, "position");
    int line = cJSON_GetObjectItem(pos, "line")->valueint;
    int character = cJSON_GetObjectItem(pos, "character")->valueint;
    
    char* path = file_uri_to_path(uri_item->valuestring);
    char* source = doc_get(uri_item->valuestring);
    if (!source) source = read_file_text(path);

    if (!source) {
        send_response(id, cJSON_CreateNull());
        free(path);
        return;
    }
    
    // 1. Parse to build registry
    Parser p;
    memset(&p, 0, sizeof(Parser));
    p.json_output = 0; p.silent = 1;
    
    Buffer bc; buf_init(&bc); p.target = &bc;
    buf_init(&p.global_bc);
    strtab_init(&p.strtab); strtab_init(&p.imports);
    reg_init(&p.registry); enum_reg_init(&p.enums);
    
    lexer_init(&p.lexer, source);
    p.current_path = path;
    advance(&p);
    parse_top_level(&p);
    
    // 2. Scan to find context
    Lexer scanner;
    lexer_init(&scanner, source);
    
    Token prev_token = {TOK_EOF, NULL, 0, 0};
    Token prev_prev_token = {TOK_EOF, NULL, 0, 0};
    
    // Calculate cursor offset
    size_t cursor_offset = 0;
    int l = 0;
    const char* ptr = source;
    while (*ptr && l < line) {
        if (*ptr == '\n') l++;
        ptr++;
    }
    cursor_offset = (ptr - source) + character;
    
    int context_is_enum_member = 0;
    char* enum_name = NULL;
    
    // Expression Context
    char* active_decorator = NULL;
    int paren_depth = 0;
    int decorator_start_depth = -1;
    int in_expr = 0;
    int in_count = 0;

    // Struct Context
    char* active_struct = NULL;
    int brace_depth = 0;
    int struct_depth = -1;

    while (1) {
        Token t = lexer_next(&scanner);
        if (t.type == TOK_EOF) break;
        
        size_t token_start = t.start - source;
        if (token_start >= cursor_offset) break; // Reached cursor
        
        // Decorator Context
        if (t.type == TOK_LPAREN) {
            paren_depth++;
            if (prev_token.type == TOK_IDENTIFIER && prev_prev_token.type == TOK_AT) {
                if (active_decorator) free(active_decorator);
                active_decorator = malloc(prev_token.length + 1);
                memcpy(active_decorator, prev_token.start, prev_token.length);
                active_decorator[prev_token.length] = '\0';
                decorator_start_depth = paren_depth; 
            }
        } else if (t.type == TOK_RPAREN) {
            if (active_decorator && paren_depth == decorator_start_depth) {
                free(active_decorator);
                active_decorator = NULL;
                decorator_start_depth = -1;
            }
            paren_depth--;
        }

        // Struct Context
        if (t.type == TOK_LBRACE) {
            brace_depth++;
            if ((prev_token.type == TOK_IDENTIFIER) && 
                (prev_prev_token.type == TOK_STRUCT || prev_prev_token.type == TOK_PACKET)) {
                 if (active_struct) free(active_struct);
                 active_struct = malloc(prev_token.length + 1);
                 memcpy(active_struct, prev_token.start, prev_token.length);
                 active_struct[prev_token.length] = '\0';
                 struct_depth = brace_depth;
            }
        } else if (t.type == TOK_EQUALS) {
             // Handle packet alias: packet P = S;
             if (prev_token.type == TOK_IDENTIFIER && prev_prev_token.type == TOK_PACKET) {
                 // We are in a packet alias definition, but we don't enter a block.
                 // We might want to track this for hover/definition, but for now just ensure we don't break context.
             }
        } else if (t.type == TOK_RBRACE) {
            if (active_struct && brace_depth == struct_depth) {
                free(active_struct);
                active_struct = NULL;
                struct_depth = -1;
            }
            brace_depth--;
        }
        
        prev_prev_token = prev_token;
        prev_token = t;
    }
    
    // Check context: [Identifier] [.] [Cursor]
    if (prev_token.type == TOK_DOT && prev_prev_token.type == TOK_IDENTIFIER) {
        // Check if prev_prev is an Enum
        EnumDef* edef = enum_reg_find(&p.enums, prev_prev_token.start, prev_prev_token.length);
        if (edef) {
            context_is_enum_member = 1;
            enum_name = malloc(prev_prev_token.length + 1);
            if (enum_name) {
                memcpy(enum_name, prev_prev_token.start, prev_prev_token.length);
                enum_name[prev_prev_token.length] = '\0';
            }
        }
    }

    if (active_decorator) {
        if (strcmp(active_decorator, "expr") == 0) in_expr = 1;
        else if (strcmp(active_decorator, "count") == 0 || strcmp(active_decorator, "len") == 0) in_count = 1;
    }
    
    // Build Completion List
    cJSON* result = cJSON_CreateObject();
    cJSON_AddBoolToObject(result, "isIncomplete", 0);
    cJSON* items = cJSON_CreateArray();
    
    if (context_is_enum_member && enum_name) {
        EnumDef* edef = enum_reg_find(&p.enums, enum_name, (int)strlen(enum_name));
        if (edef) {
            for (size_t i = 0; i < edef->count; i++) {
                cJSON* item = cJSON_CreateObject();
                cJSON* label = cJSON_CreateString(edef->values[i].name);
                cJSON_AddItemToObject(item, "label", label);
                cJSON_AddNumberToObject(item, "kind", 20); // EnumMember
                cJSON_AddItemToArray(items, item);
            }
        }
        free(enum_name);
    } else if (in_expr || in_count) {
        if (in_expr) {
            // Math Functions
            const char* math_funcs[] = { "sin", "cos", "tan", "sqrt", "log", "abs", "pow" };
            for (size_t i = 0; i < sizeof(math_funcs)/sizeof(char*); i++) {
                cJSON* item = cJSON_CreateObject();
                cJSON_AddStringToObject(item, "label", math_funcs[i]);
                cJSON_AddNumberToObject(item, "kind", 3); // Function
                cJSON_AddStringToObject(item, "detail", "Math Function");
                cJSON_AddItemToArray(items, item);
            }
            
            // Type Conversions
            const char* type_convs[] = { "int", "float" };
            for (size_t i = 0; i < sizeof(type_convs)/sizeof(char*); i++) {
                cJSON* item = cJSON_CreateObject();
                cJSON_AddStringToObject(item, "label", type_convs[i]);
                cJSON_AddNumberToObject(item, "kind", 3); // Function
                cJSON_AddStringToObject(item, "detail", "Type Conversion");
                cJSON_AddItemToArray(items, item);
            }
        }

        // Struct Fields
        if (active_struct) {
            StructDef* sdef = reg_find(&p.registry, active_struct, (int)strlen(active_struct));
            if (sdef && sdef->bytecode.data) {
                const uint8_t* bc_ptr = sdef->bytecode.data;
                const uint8_t* end = bc_ptr + sdef->bytecode.size;
                while (bc_ptr < end) {
                    uint8_t op = read_u8(&bc_ptr, end);
                    if (op == OP_LOAD_CTX) {
                        uint16_t key = read_u16(&bc_ptr, end);
                        if (key < p.strtab.count) {
                            cJSON* item = cJSON_CreateObject();
                            cJSON_AddStringToObject(item, "label", p.strtab.strings[key]);
                            cJSON_AddNumberToObject(item, "kind", 5); // Field
                            cJSON_AddStringToObject(item, "detail", "Field");
                            cJSON_AddItemToArray(items, item);
                        }
                    } else {
                        skip_instruction(&bc_ptr, end, op);
                    }
                }
            }
        }
    } else {
        // Top-level items
        const char* keywords[] = { "struct", "packet", "enum", "import", "true", "false", "prefix", "string", "const", "range", "if", "else", "switch", "case", "default" };
        for (size_t i = 0; i < sizeof(keywords)/sizeof(char*); i++) {
            cJSON* item = cJSON_CreateObject();
            cJSON_AddStringToObject(item, "label", keywords[i]);
            cJSON_AddNumberToObject(item, "kind", 14); // Keyword
            cJSON_AddItemToArray(items, item);
        }
        
        // Add Decorators
        for (int i = 0; BUILTIN_DECORATORS[i].name; i++) {
            cJSON* item = cJSON_CreateObject();
            // Add @ prefix to label for convenience, or just name? 
            // Usually completion is triggered by @, so maybe just name?
            // But if triggered by space, @name is better.
            // Let's provide both or just @name.
            // If user typed @, VS Code filters.
            char label[64];
            snprintf(label, sizeof(label), "@%s", BUILTIN_DECORATORS[i].name);
            cJSON_AddStringToObject(item, "label", label);
            cJSON_AddNumberToObject(item, "kind", 3); // Function/Method-like
            cJSON_AddStringToObject(item, "detail", BUILTIN_DECORATORS[i].detail);
            cJSON_AddStringToObject(item, "documentation", BUILTIN_DECORATORS[i].doc);
            cJSON_AddItemToArray(items, item);
        }
        
        // Add Structs
        for (size_t i = 0; i < p.registry.count; i++) {
            cJSON* item = cJSON_CreateObject();
            cJSON_AddStringToObject(item, "label", p.registry.defs[i].name);
            cJSON_AddNumberToObject(item, "kind", 7); // Class/Struct
            if (p.registry.defs[i].doc_comment) cJSON_AddStringToObject(item, "detail", p.registry.defs[i].doc_comment);
            cJSON_AddItemToArray(items, item);
        }
        
        // Add Enums
        for (size_t i = 0; i < p.enums.count; i++) {
            cJSON* item = cJSON_CreateObject();
            cJSON_AddStringToObject(item, "label", p.enums.defs[i].name);
            cJSON_AddNumberToObject(item, "kind", 13); // Enum
            if (p.enums.defs[i].doc_comment) cJSON_AddStringToObject(item, "detail", p.enums.defs[i].doc_comment);
            cJSON_AddItemToArray(items, item);
        }
    }
    
    cJSON_AddItemToObject(result, "items", items);
    send_response(id, result);
    
    // Cleanup
    if (active_decorator) free(active_decorator);
    if (active_struct) free(active_struct);
    buf_free(&bc); buf_free(&p.global_bc);
    strtab_free(&p.strtab); strtab_free(&p.imports);
    reg_free(&p.registry); enum_reg_free(&p.enums);
    if (p.errors) {
        for (int i = 0; i < p.error_count; i++) free(p.errors[i].message);
        free(p.errors);
    }
    free(source); free(path);
}

static void handle_formatting(cJSON* id, cJSON* params) {
    cJSON* doc = cJSON_GetObjectItem(params, "textDocument");
    cJSON* uri_item = cJSON_GetObjectItem(doc, "uri");
    
    char* path = file_uri_to_path(uri_item->valuestring);
    char* source = read_file_text(path);
    if (!source) {
        send_response(id, cJSON_CreateNull());
        free(path);
        return;
    }
    
    char* formatted = cnd_format_source(source);
    
    cJSON* edits = cJSON_CreateArray();
    cJSON* edit = cJSON_CreateObject();
    
    cJSON* range = cJSON_CreateObject();
    cJSON* start = cJSON_CreateObject(); cJSON_AddNumberToObject(start, "line", 0); cJSON_AddNumberToObject(start, "character", 0);
    cJSON* end = cJSON_CreateObject(); cJSON_AddNumberToObject(end, "line", 999999); cJSON_AddNumberToObject(end, "character", 0);
    
    cJSON_AddItemToObject(range, "start", start);
    cJSON_AddItemToObject(range, "end", end);
    
    cJSON_AddItemToObject(edit, "range", range);
    cJSON_AddStringToObject(edit, "newText", formatted ? formatted : source);
    
    cJSON_AddItemToArray(edits, edit);
    
    send_response(id, edits);
    
    free(source);
    if (formatted) free(formatted);
    free(path);
}

static void handle_document_symbol(cJSON* id, cJSON* params) {
    cJSON* doc = cJSON_GetObjectItem(params, "textDocument");
    cJSON* uri_item = cJSON_GetObjectItem(doc, "uri");
    
    char* path = file_uri_to_path(uri_item->valuestring);
    char* source = read_file_text(path);
    if (!source) {
        send_response(id, cJSON_CreateNull());
        free(path);
        return;
    }
    
    // Parse
    Parser p;
    memset(&p, 0, sizeof(Parser));
    p.json_output = 0; p.silent = 1;
    Buffer bc; buf_init(&bc); p.target = &bc;
    buf_init(&p.global_bc); strtab_init(&p.strtab); strtab_init(&p.imports); reg_init(&p.registry); enum_reg_init(&p.enums);
    
    lexer_init(&p.lexer, source);
    p.current_path = path;
    advance(&p);
    parse_top_level(&p);
    
    cJSON* symbols = cJSON_CreateArray();
    
    // Structs
    for (size_t i = 0; i < p.registry.count; i++) {
        cJSON* sym = cJSON_CreateObject();
        cJSON_AddStringToObject(sym, "name", p.registry.defs[i].name);
        cJSON_AddNumberToObject(sym, "kind", 23); // Struct
        
        cJSON* loc = cJSON_CreateObject();
        cJSON_AddStringToObject(loc, "uri", uri_item->valuestring);
        cJSON* range = cJSON_CreateObject();
        cJSON* start = cJSON_CreateObject(); cJSON_AddNumberToObject(start, "line", p.registry.defs[i].line - 1); cJSON_AddNumberToObject(start, "character", 0);
        cJSON* end = cJSON_CreateObject(); cJSON_AddNumberToObject(end, "line", p.registry.defs[i].line - 1); cJSON_AddNumberToObject(end, "character", 0);
        cJSON_AddItemToObject(range, "start", start); cJSON_AddItemToObject(range, "end", end);
        cJSON_AddItemToObject(loc, "range", range);
        
        cJSON_AddItemToObject(sym, "location", loc);
        cJSON_AddItemToArray(symbols, sym);
    }
    
    // Enums
    for (size_t i = 0; i < p.enums.count; i++) {
        cJSON* sym = cJSON_CreateObject();
        cJSON_AddStringToObject(sym, "name", p.enums.defs[i].name);
        cJSON_AddNumberToObject(sym, "kind", 10); // Enum
        
        cJSON* loc = cJSON_CreateObject();
        cJSON_AddStringToObject(loc, "uri", uri_item->valuestring);
        cJSON* range = cJSON_CreateObject();
        cJSON* start = cJSON_CreateObject(); cJSON_AddNumberToObject(start, "line", p.enums.defs[i].line - 1); cJSON_AddNumberToObject(start, "character", 0);
        cJSON* end = cJSON_CreateObject(); cJSON_AddNumberToObject(end, "line", p.enums.defs[i].line - 1); cJSON_AddNumberToObject(end, "character", 0);
        cJSON_AddItemToObject(range, "start", start); cJSON_AddItemToObject(range, "end", end);
        cJSON_AddItemToObject(loc, "range", range);
        
        cJSON_AddItemToObject(sym, "location", loc);
        cJSON_AddItemToArray(symbols, sym);
    }
    
    send_response(id, symbols);
    
    buf_free(&bc); buf_free(&p.global_bc);
    strtab_free(&p.strtab); strtab_free(&p.imports);
    reg_free(&p.registry); enum_reg_free(&p.enums);
    if (p.errors) {
        for (int i = 0; i < p.error_count; i++) free(p.errors[i].message);
        free(p.errors);
    }
    free(source); free(path);
}

static void publish_diagnostics(const char* uri, const char* source) {
    // Parse to collect errors
    Parser p;
    memset(&p, 0, sizeof(Parser));
    p.json_output = 0; p.silent = 1;
    Buffer bc; buf_init(&bc); p.target = &bc;
    buf_init(&p.global_bc); strtab_init(&p.strtab); strtab_init(&p.imports); reg_init(&p.registry); enum_reg_init(&p.enums);
    
    lexer_init(&p.lexer, source);
    char* path = file_uri_to_path(uri);
    p.current_path = path;
    advance(&p);
    parse_top_level(&p);
    
    // Send diagnostics
    cJSON* notification = cJSON_CreateObject();
    cJSON_AddStringToObject(notification, "jsonrpc", "2.0");
    cJSON_AddStringToObject(notification, "method", "textDocument/publishDiagnostics");
    
    cJSON* params = cJSON_CreateObject();
    cJSON_AddStringToObject(params, "uri", uri);
    
    cJSON* diagnostics = cJSON_CreateArray();
    
    for (size_t i = 0; i < (size_t)p.error_count; i++) {
        if (i >= (size_t)p.error_cap || !p.errors) break;
        CompilerError* err = &p.errors[i];
        
        cJSON* diag = cJSON_CreateObject();
        
        cJSON* range = cJSON_CreateObject();
        cJSON* start = cJSON_CreateObject(); cJSON_AddNumberToObject(start, "line", err->line - 1); cJSON_AddNumberToObject(start, "character", err->column - 1);
        cJSON* end = cJSON_CreateObject(); cJSON_AddNumberToObject(end, "line", err->line - 1); cJSON_AddNumberToObject(end, "character", err->column); // Just 1 char width or more?
        cJSON_AddItemToObject(range, "start", start); cJSON_AddItemToObject(range, "end", end);
        
        cJSON_AddItemToObject(diag, "range", range);
        cJSON_AddNumberToObject(diag, "severity", 1); // Error
        cJSON_AddStringToObject(diag, "message", err->message);
        cJSON_AddStringToObject(diag, "source", "concordia");
        
        cJSON_AddItemToArray(diagnostics, diag);
        
        free(err->message);
    }
    if (p.errors) free(p.errors);
    
    cJSON_AddItemToObject(params, "diagnostics", diagnostics);
    cJSON_AddItemToObject(notification, "params", params);
    
    send_json(notification);
    cJSON_Delete(notification);
    
    buf_free(&bc); buf_free(&p.global_bc);
    strtab_free(&p.strtab); strtab_free(&p.imports);
    reg_free(&p.registry); enum_reg_free(&p.enums);
    // p.errors already freed in loop above
    free(path);
}

static void handle_didOpen(cJSON* params) {
    cJSON* doc = cJSON_GetObjectItem(params, "textDocument");
    if (!doc) return;
    cJSON* uri = cJSON_GetObjectItem(doc, "uri");
    cJSON* text = cJSON_GetObjectItem(doc, "text");
    if (uri && text && uri->valuestring && text->valuestring) {
        doc_update(uri->valuestring, text->valuestring);
        publish_diagnostics(uri->valuestring, text->valuestring);
    }
}

static void handle_didChange(cJSON* params) {
    cJSON* doc = cJSON_GetObjectItem(params, "textDocument");
    if (!doc) return;
    cJSON* uri = cJSON_GetObjectItem(doc, "uri");
    cJSON* changes = cJSON_GetObjectItem(params, "contentChanges");
    if (uri && changes && cJSON_GetArraySize(changes) > 0) {
        // Full sync: last change has full text
        cJSON* lastChange = cJSON_GetArrayItem(changes, cJSON_GetArraySize(changes) - 1);
        cJSON* text = cJSON_GetObjectItem(lastChange, "text");
        if (text && text->valuestring) {
            doc_update(uri->valuestring, text->valuestring);
            publish_diagnostics(uri->valuestring, text->valuestring);
        }
    }
}

static void handle_didSave(cJSON* params) {
    cJSON* doc = cJSON_GetObjectItem(params, "textDocument");
    if (!doc) return;
    cJSON* uri = cJSON_GetObjectItem(doc, "uri");
    if (uri && uri->valuestring) {
        // We need source content. If it's a save, we can read from disk.
        char* path = file_uri_to_path(uri->valuestring);
        char* source = read_file_text(path);
        if (source) {
            publish_diagnostics(uri->valuestring, source);
            free(source);
        }
        free(path);
    }
}

static void handle_initialize(cJSON* id) {
    cJSON* res = cJSON_CreateObject();
    cJSON* cap = cJSON_CreateObject();
    cJSON_AddBoolToObject(cap, "definitionProvider", 1);
    cJSON_AddBoolToObject(cap, "hoverProvider", 1);
    cJSON_AddBoolToObject(cap, "documentSymbolProvider", 1);
    cJSON_AddBoolToObject(cap, "documentFormattingProvider", 1);
    
    cJSON* comp = cJSON_CreateObject();
    cJSON_AddBoolToObject(comp, "resolveProvider", 0);
    cJSON_AddStringToObject(comp, "triggerCharacters", "."); 
    cJSON_AddItemToObject(cap, "completionProvider", comp);
    
    // Advertise text document sync (1 = Full)
    cJSON_AddNumberToObject(cap, "textDocumentSync", 1);
    
    cJSON_AddItemToObject(res, "capabilities", cap);
    send_response(id, res);
}

static void handle_definition(cJSON* id, cJSON* params) {
    cJSON* doc = cJSON_GetObjectItem(params, "textDocument");
    cJSON* uri_item = cJSON_GetObjectItem(doc, "uri");
    cJSON* pos = cJSON_GetObjectItem(params, "position");
    int line = cJSON_GetObjectItem(pos, "line")->valueint;
    int character = cJSON_GetObjectItem(pos, "character")->valueint;
    
    char* path = file_uri_to_path(uri_item->valuestring);
    
    // Read file content
    char* source = doc_get(uri_item->valuestring);
    if (!source) source = read_file_text(path);

    if (!source) {
        send_response(id, cJSON_CreateNull());
        free(path);
        return;
    }
    
    AnalysisResult res;
    analyze_source(source, path, line, character, &res);
    
    if (res.found) {
        cJSON* loc = cJSON_CreateObject();
        
        // Use resolved file if available, else fallback to current URI
        if (res.def_file) {
            // Convert file path to URI
            // Hacky: assume absolute path or simple relative?
            // VS Code expects file:///...
            // If def_file is "import_a.cnd" (relative), and we opened "import_b.cnd" at "/abs/path/to/import_b.cnd",
            // the resolve_path in parser made it absolute. So res.def_file should be absolute.
            char uri[1024];
            // Windows path handling
            if (res.def_file[1] == ':') {
                snprintf(uri, sizeof(uri), "file:///%s", res.def_file);
            } else {
                snprintf(uri, sizeof(uri), "file://%s", res.def_file);
            }
            // Normalize backslashes to slashes for URI
            for(int i=0; uri[i]; i++) if(uri[i] == '\\') uri[i] = '/';
            
            cJSON_AddStringToObject(loc, "uri", uri);
        } else {
            cJSON_AddStringToObject(loc, "uri", uri_item->valuestring);
        }

        cJSON* range = cJSON_CreateObject();
        cJSON* start = cJSON_CreateObject();
        cJSON_AddNumberToObject(start, "line", res.def_line);
        cJSON_AddNumberToObject(start, "character", 0);
        cJSON* end = cJSON_CreateObject();
        cJSON_AddNumberToObject(end, "line", res.def_line);
        cJSON_AddNumberToObject(end, "character", 0); // Highlight whole line? Or just start
        
        cJSON_AddItemToObject(range, "start", start);
        cJSON_AddItemToObject(range, "end", end);
        cJSON_AddItemToObject(loc, "range", range);
        
        send_response(id, loc);
        if (res.symbol_name) free(res.symbol_name);
        if (res.def_file) free(res.def_file);
        if (res.doc_comment) free(res.doc_comment);
    } else {
        send_response(id, cJSON_CreateNull());
    }
    
    free(source);
    free(path);
}

static void handle_hover(cJSON* id, cJSON* params) {
    // Similar to definition, but returns markdown
    cJSON* doc = cJSON_GetObjectItem(params, "textDocument");
    cJSON* uri_item = cJSON_GetObjectItem(doc, "uri");
    cJSON* pos = cJSON_GetObjectItem(params, "position");
    int line = cJSON_GetObjectItem(pos, "line")->valueint;
    int character = cJSON_GetObjectItem(pos, "character")->valueint;
    
    char* path = file_uri_to_path(uri_item->valuestring);
    char* source = doc_get(uri_item->valuestring);
    if (!source) source = read_file_text(path);

    if (!source) {
        send_response(id, cJSON_CreateNull());
        free(path);
        return;
    }
    
    AnalysisResult res;
    analyze_source(source, path, line, character, &res);
    
    if (res.found) {
        cJSON* h = cJSON_CreateObject();
        cJSON* contents = cJSON_CreateObject();
        cJSON_AddStringToObject(contents, "kind", "markdown");
        
        char msg[4096]; // Increased buffer size
        const char* file_display = res.def_file ? res.def_file : "current file";
        
        int offset = 0;
        offset += snprintf(msg + offset, sizeof(msg) - offset, "**%s**\n\nDefined in %s on line %d.", res.symbol_name, file_display, res.def_line + 1);
        
        if (res.doc_comment) {
            offset += snprintf(msg + offset, sizeof(msg) - offset, "\n\n%s", res.doc_comment);
        }

        if (res.type_details) {
            offset += snprintf(msg + offset, sizeof(msg) - offset, "\n\n%s", res.type_details);
        }
        
        cJSON_AddStringToObject(contents, "value", msg);
        
        cJSON_AddItemToObject(h, "contents", contents);
        send_response(id, h);
        if (res.symbol_name) free(res.symbol_name);
        if (res.def_file) free(res.def_file);
        if (res.doc_comment) free(res.doc_comment);
        if (res.type_details) free(res.type_details);
    } else {
        send_response(id, cJSON_CreateNull());
    }
    
    free(source);
    free(path);
}

int cmd_lsp(int argc, char** argv) {
    (void)argc; (void)argv;
#ifdef _WIN32
    _setmode(_fileno(stdin), _O_BINARY);
    _setmode(_fileno(stdout), _O_BINARY);
#endif

    // Standard LSP loop
    while (1) {
        cJSON* req = read_json();
        if (!req) break;
        
        cJSON* id = cJSON_GetObjectItem(req, "id");
        cJSON* method = cJSON_GetObjectItem(req, "method");
        cJSON* params = cJSON_GetObjectItem(req, "params");
        
        if (method && method->valuestring) {
            if (strcmp(method->valuestring, "initialize") == 0) {
                handle_initialize(id);
            } else if (strcmp(method->valuestring, "textDocument/definition") == 0) {
                handle_definition(id, params);
            } else if (strcmp(method->valuestring, "textDocument/hover") == 0) {
                handle_hover(id, params);
            } else if (strcmp(method->valuestring, "textDocument/completion") == 0) {
                handle_completion(id, params);
            } else if (strcmp(method->valuestring, "textDocument/formatting") == 0) {
                handle_formatting(id, params);
            } else if (strcmp(method->valuestring, "textDocument/documentSymbol") == 0) {
                handle_document_symbol(id, params);
            } else if (strcmp(method->valuestring, "textDocument/didOpen") == 0) {
                handle_didOpen(params);
            } else if (strcmp(method->valuestring, "textDocument/didChange") == 0) {
                handle_didChange(params);
            } else if (strcmp(method->valuestring, "textDocument/didSave") == 0) {
                handle_didSave(params);
            } else if (strcmp(method->valuestring, "shutdown") == 0) {
                send_response(id, cJSON_CreateNull());
            } else if (strcmp(method->valuestring, "exit") == 0) {
                cJSON_Delete(req);
                return 0;
            }
        }
        
        cJSON_Delete(req);
    }
    return 0;
}
