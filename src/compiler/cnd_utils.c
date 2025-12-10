#include "cnd_internal.h"

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
