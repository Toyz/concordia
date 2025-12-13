#include "cnd_internal.h"

char* cnd_canonicalize_path(const char* path) {
    if (!path) return NULL;

#ifdef _WIN32
    // _fullpath resolves relative segments and returns an absolute path.
    char* abs_path = _fullpath(NULL, path, 0);
    if (!abs_path) {
        return strdup(path);
    }

    // Normalize for stable comparisons (Windows is case-insensitive).
    for (char* c = abs_path; *c; c++) {
        if (*c == '\\') *c = '/';
        *c = (char)tolower((unsigned char)*c);
    }
    return abs_path;
#else
    // realpath resolves symlinks and normalizes ./ and ../ segments.
    // It fails if the path doesn't exist; fall back to strdup in that case.
    char* rp = realpath(path, NULL);
    if (!rp) {
        return strdup(path);
    }
    return rp;
#endif
}

// Helper to get opcode instruction size (returns total bytes including opcode)
static int get_opcode_size_and_keyid_offset(uint8_t op, int* keyid_offset) {
    *keyid_offset = -1; // -1 means no key ID in this instruction
    
    switch (op) {
        case OP_NOOP:
        case OP_SET_ENDIAN_LE:
        case OP_SET_ENDIAN_BE:
        case OP_EXIT_STRUCT:
        case OP_ARR_END:
        case OP_EXIT_BIT_MODE:
        case OP_ENTER_BIT_MODE:
        case OP_ADD:
        case OP_SUB:
        case OP_MUL:
        case OP_DIV:
        case OP_MOD:
        case OP_NEG:
        case OP_LOG_AND:
        case OP_LOG_OR:
        case OP_LOG_NOT:
        case OP_BIT_AND:
        case OP_BIT_OR:
        case OP_BIT_XOR:
        case OP_BIT_NOT:
        case OP_SHL:
        case OP_SHR:
        case OP_EQ:
        case OP_NEQ:
        case OP_GT:
        case OP_LT:
        case OP_GTE:
        case OP_LTE:
        case OP_POP:
        case OP_DUP:
        case OP_SWAP:
        case OP_FADD:
        case OP_FSUB:
        case OP_FMUL:
        case OP_FDIV:
        case OP_FNEG:
        case OP_SIN:
        case OP_COS:
        case OP_TAN:
        case OP_SQRT:
        case OP_POW:
        case OP_LOG:
        case OP_ABS:
        case OP_ITOF:
        case OP_FTOI:
        case OP_EQ_F:
        case OP_NEQ_F:
        case OP_GT_F:
        case OP_LT_F:
        case OP_GTE_F:
        case OP_LTE_F:
            return 1;
            
        case OP_ALIGN_PAD:
        case OP_ALIGN_FILL:
        case OP_EMIT:
            return 2;
            
        // Opcodes with key_id at offset 1
        case OP_ENTER_STRUCT:
        case OP_META_NAME:
        case OP_META_VERSION:
        case OP_IO_U8:
        case OP_IO_U16:
        case OP_IO_U32:
        case OP_IO_U64:
        case OP_IO_I8:
        case OP_IO_I16:
        case OP_IO_I32:
        case OP_IO_I64:
        case OP_IO_F32:
        case OP_IO_F64:
        case OP_IO_BOOL:
        case OP_IO_BIT_BOOL:
        case OP_LOAD_CTX:
        case OP_STORE_CTX:
        case OP_ARR_EOF:
            *keyid_offset = 1;
            return 3;
            
        case OP_IO_BIT_U:
        case OP_IO_BIT_I:
            *keyid_offset = 1;
            return 4; // op + key(2) + bits(1)
            
        case OP_STR_NULL:
            *keyid_offset = 1;
            return 5; // op + key(2) + maxlen(2)
            
        case OP_STR_PRE_U8:
        case OP_STR_PRE_U16:
        case OP_STR_PRE_U32:
        case OP_ARR_PRE_U8:
        case OP_ARR_PRE_U16:
        case OP_ARR_PRE_U32:
            *keyid_offset = 1;
            return 3;
            
        case OP_ARR_FIXED:
        case OP_RAW_BYTES:
            *keyid_offset = 1;
            return 7; // op + key(2) + count(4)
            
        case OP_ARR_DYNAMIC:
            *keyid_offset = 1;
            return 5; // op + key(2) + ref_key(2)
            
        case OP_JUMP:
        case OP_JUMP_IF_NOT:
            return 5; // op + offset(4)
            
        case OP_PUSH_IMM:
            return 9; // op + val(8)
            
        case OP_SWITCH:
            *keyid_offset = 1;
            return 7; // op + key(2) + table_off(4)
            
        // Variable length - skip for now, struct bytecode shouldn't have these
        case OP_CONST_CHECK:
        case OP_CONST_WRITE:
        case OP_RANGE_CHECK:
        case OP_SCALE_LIN:
        case OP_TRANS_ADD:
        case OP_TRANS_SUB:
        case OP_TRANS_MUL:
        case OP_TRANS_DIV:
        case OP_TRANS_POLY:
        case OP_TRANS_SPLINE:
        case OP_CRC_16:
        case OP_CRC_32:
        case OP_ENUM_CHECK:
        case OP_MARK_OPTIONAL:
        default:
            return -1; // Unknown/variable - can't process
    }
}

// Append struct bytecode with key IDs remapped to include prefix
void buf_append_with_prefix(Buffer* b, const uint8_t* src, size_t len, 
                            const char* prefix, int prefix_len, StringTable* strtab) {
    size_t i = 0;
    while (i < len) {
        uint8_t op = src[i];
        int keyid_offset;
        int instr_size = get_opcode_size_and_keyid_offset(op, &keyid_offset);
        
        if (instr_size < 0 || i + instr_size > len) {
            // Unknown opcode or would overflow - just copy rest verbatim
            buf_append(b, src + i, len - i);
            break;
        }
        
        if (keyid_offset < 0) {
            // No key ID to remap, just copy
            buf_append(b, src + i, instr_size);
        } else {
            // Has key ID - need to remap
            // Copy opcode byte(s) before key
            buf_append(b, src + i, keyid_offset);
            
            // Read old key ID
            uint16_t old_key = src[i + keyid_offset] | (src[i + keyid_offset + 1] << 8);
            
            // Look up old string
            const char* old_name = (old_key < strtab->count) ? strtab->strings[old_key] : "";
            int old_len = (int)strlen(old_name);
            
            // Build new prefixed name
            char new_name[512];
            if (prefix_len + 1 + old_len < 512) {
                memcpy(new_name, prefix, prefix_len);
                new_name[prefix_len] = '.';
                memcpy(new_name + prefix_len + 1, old_name, old_len);
                new_name[prefix_len + 1 + old_len] = '\0';
            } else {
                // Fallback - just use old name
                memcpy(new_name, old_name, old_len + 1);
            }
            
            // Add new name to strtab and get new key ID
            uint16_t new_key = strtab_add(strtab, new_name, prefix_len + 1 + old_len);
            
            // Write new key ID
            buf_push(b, new_key & 0xFF);
            buf_push(b, (new_key >> 8) & 0xFF);
            
            // Copy rest of instruction after key ID
            int after_key = instr_size - keyid_offset - 2;
            if (after_key > 0) {
                buf_append(b, src + i + keyid_offset + 2, after_key);
            }
        }
        
        i += instr_size;
    }
}

// --- Buffer Implementation ---

void buf_init(Buffer* b) {
    b->size = 0;
    b->capacity = 1024;
    b->data = malloc(b->capacity);
}

void buf_free(Buffer* b) {
    free(b->data);
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

void buf_push_u64(Buffer* b, uint64_t val) {
    buf_push_u32(b, val & 0xFFFFFFFF);
    buf_push_u32(b, (val >> 32) & 0xFFFFFFFF);
}

void buf_write_u16_at(Buffer* b, size_t offset, uint16_t val) {
    if (offset + 2 > b->size) return;
    b->data[offset] = val & 0xFF;
    b->data[offset + 1] = (val >> 8) & 0xFF;
}

void buf_write_u32_at(Buffer* b, size_t offset, uint32_t val) {
    if (offset + 4 > b->size) return;
    b->data[offset] = val & 0xFF;
    b->data[offset + 1] = (val >> 8) & 0xFF;
    b->data[offset + 2] = (val >> 16) & 0xFF;
    b->data[offset + 3] = (val >> 24) & 0xFF;
}

void buf_write_u8_at(Buffer* b, size_t offset, uint8_t val) {
    if (offset + 1 > b->size) return;
    b->data[offset] = val;
}

size_t buf_current_offset(Buffer* b) {
    return b->size;
}

// --- String Table Implementation ---

void strtab_init(StringTable* t) {
    t->count = 0;
    t->capacity = 32;
    t->strings = malloc(t->capacity * sizeof(char*));
}

uint16_t strtab_add(StringTable* t, const char* start, int len) {
    for (size_t i = 0; i < t->count; i++) {
        if (strlen(t->strings[i]) == (size_t)len && strncmp(t->strings[i], start, len) == 0) {
            return (uint16_t)i;
        }
    }
    if (t->count >= t->capacity) {
        t->capacity = (t->capacity == 0) ? 8 : t->capacity * 2;
        t->strings = realloc(t->strings, t->capacity * sizeof(char*));
    }
    char* copy = malloc(len + 1);
    memcpy(copy, start, len);
    copy[len] = '\0';
    t->strings[t->count] = copy;
    return (uint16_t)t->count++;
}

void strtab_free(StringTable* t) {
    for (size_t i = 0; i < t->count; i++) {
        free(t->strings[i]);
    }
    free(t->strings);
    t->count = 0;
    t->capacity = 0;
    t->strings = NULL;
}

// --- Registry Implementation ---

void reg_init(StructRegistry* r) {
    r->count = 0;
    r->capacity = 8;
    r->defs = malloc(r->capacity * sizeof(StructDef));
}

void reg_free(StructRegistry* r) {
    for (size_t i = 0; i < r->count; i++) {
        free(r->defs[i].name);
        if (r->defs[i].file) free(r->defs[i].file);
        if (r->defs[i].doc_comment) free(r->defs[i].doc_comment);
        buf_free(&r->defs[i].bytecode);
    }
    free(r->defs);
    r->count = 0;
    r->capacity = 0;
    r->defs = NULL;
}

StructDef* reg_add(StructRegistry* r, const char* name, int len, int line, const char* file, const char* doc) {
    if (r->count >= r->capacity) {
        r->capacity = (r->capacity == 0) ? 8 : r->capacity * 2;
        r->defs = realloc(r->defs, r->capacity * sizeof(StructDef));
    }
    StructDef* def = &r->defs[r->count++];
    def->name = malloc(len + 1);
    memcpy(def->name, name, len);
    def->name[len] = '\0';
    def->line = line;
    def->file = file ? strdup(file) : NULL;
    def->doc_comment = doc ? strdup(doc) : NULL;
    buf_init(&def->bytecode);
    return def;
}

StructDef* reg_find(StructRegistry* r, const char* name, int len) {
    for (size_t i = 0; i < r->count; i++) {
        if (strlen(r->defs[i].name) == (size_t)len && strncmp(r->defs[i].name, name, len) == 0) {
            return &r->defs[i];
        }
    }
    return NULL;
}

void enum_reg_init(EnumRegistry* r) {
    r->count = 0;
    r->capacity = 8;
    r->defs = malloc(r->capacity * sizeof(EnumDef));
}

void enum_reg_free(EnumRegistry* r) {
    for (size_t i = 0; i < r->count; i++) {
        free(r->defs[i].name);
        if (r->defs[i].file) free(r->defs[i].file);
        if (r->defs[i].doc_comment) free(r->defs[i].doc_comment);
        for (size_t j = 0; j < r->defs[i].count; j++) {
            free(r->defs[i].values[j].name);
            if (r->defs[i].values[j].doc_comment) free(r->defs[i].values[j].doc_comment);
        }
        free(r->defs[i].values);
    }
    free(r->defs);
    r->count = 0;
    r->capacity = 0;
    r->defs = NULL;
}

EnumDef* enum_reg_add(EnumRegistry* r, const char* name, int len, int line, const char* file, const char* doc) {
    if (r->count >= r->capacity) {
        r->capacity = (r->capacity == 0) ? 8 : r->capacity * 2;
        r->defs = realloc(r->defs, r->capacity * sizeof(EnumDef));
    }
    EnumDef* def = &r->defs[r->count++];
    def->name = malloc(len + 1);
    memcpy(def->name, name, len);
    def->name[len] = '\0';
    def->line = line;
    def->file = file ? strdup(file) : NULL;
    def->doc_comment = doc ? strdup(doc) : NULL;
    def->values = NULL;
    def->count = 0;
    def->capacity = 0;
    def->underlying_type = 0x12; // Default U32 (OP_IO_U32)
    return def;
}

EnumDef* enum_reg_find(EnumRegistry* r, const char* name, int len) {
    for (size_t i = 0; i < r->count; i++) {
        if (strlen(r->defs[i].name) == (size_t)len && strncmp(r->defs[i].name, name, len) == 0) {
            return &r->defs[i];
        }
    }
    return NULL;
}

// --- Number Parsing ---

int hex_char_to_int(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return 0;
}

uint32_t parse_number_u32(const char* start, int length) {
    uint32_t res = 0;
    // Handle hex
    if (length > 2 && start[0] == '0' && (start[1] == 'x' || start[1] == 'X')) {
        for (int i = 2; i < length; i++) {
            res = (res << 4) | hex_char_to_int(start[i]);
        }
    } else {
        for (int i = 0; i < length; i++) {
            if (start[i] >= '0' && start[i] <= '9') {
                res = res * 10 + (start[i] - '0');
            }
        }
    }
    return res;
}

int64_t parse_number_i64(const char* start, int length) {
    const char* s = start;
    const char* end = s + length;
    if (s == end) return 0;
    
    int neg = 0;
    if (*s == '-') {
        neg = 1;
        s++;
    } else if (*s == '+') {
        s++;
    }
    
    uint64_t res = 0;
    if (end - s > 2 && *s == '0' && (s[1] == 'x' || s[1] == 'X')) {
        s += 2;
        while (s < end) {
            res = (res << 4) | hex_char_to_int(*s++);
        }
    } else {
        while (s < end) {
            char c = *s++;
            if (c >= '0' && c <= '9') {
                res = res * 10 + (c - '0');
            }
        }
    }
    
    return neg ? -(int64_t)res : (int64_t)res;
}

double parse_number_double(const char* start, int length) {
    char buf[64];
    if (length >= 63) return 0.0;
    memcpy(buf, start, length);
    buf[length] = '\0';
    return strtod(buf, NULL);
}

// --- StringBuilder Implementation ---

void sb_init(StringBuilder* sb) {
    sb->length = 0;
    sb->capacity = 64;
    sb->data = malloc(sb->capacity);
    sb->data[0] = '\0';
}

void sb_free(StringBuilder* sb) {
    if (sb->data) free(sb->data);
    sb->data = NULL;
    sb->length = 0;
    sb->capacity = 0;
}

void sb_append(StringBuilder* sb, const char* str) {
    if (!str) return;
    size_t len = strlen(str);
    sb_append_n(sb, str, len);
}

void sb_append_n(StringBuilder* sb, const char* str, size_t len) {
    if (sb->length + len + 1 > sb->capacity) {
        while (sb->length + len + 1 > sb->capacity) {
            sb->capacity = (sb->capacity == 0) ? 64 : sb->capacity * 2;
        }
        sb->data = realloc(sb->data, sb->capacity);
    }
    memcpy(sb->data + sb->length, str, len);
    sb->length += len;
    sb->data[sb->length] = '\0';
}

void sb_append_c(StringBuilder* sb, char c) {
    if (sb->length + 1 + 1 > sb->capacity) {
        while (sb->length + 1 + 1 > sb->capacity) {
            sb->capacity = (sb->capacity == 0) ? 64 : sb->capacity * 2;
        }
        sb->data = realloc(sb->data, sb->capacity);
    }
    sb->data[sb->length++] = c;
    sb->data[sb->length] = '\0';
}

void sb_reset(StringBuilder* sb) {
    sb->length = 0;
    if (sb->data) sb->data[0] = '\0';
}

char* sb_build(StringBuilder* sb) {
    char* str = malloc(sb->length + 1);
    memcpy(str, sb->data, sb->length);
    str[sb->length] = '\0';
    return str;
}

