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
        t->capacity *= 2;
        t->strings = realloc(t->strings, t->capacity * sizeof(char*));
    }
    char* copy = malloc(len + 1);
    memcpy(copy, start, len);
    copy[len] = '\0';
    t->strings[t->count] = copy;
    return (uint16_t)t->count++;
}

// --- Registry Implementation ---

void reg_init(StructRegistry* r) {
    r->count = 0;
    r->capacity = 8;
    r->defs = malloc(r->capacity * sizeof(StructDef));
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
