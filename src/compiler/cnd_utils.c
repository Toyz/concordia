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

// --- String Table Implementation ---

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

// --- Registry Implementation ---

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
