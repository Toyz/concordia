#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "cli_helpers.h"

// =================================================================================================
// File IO Helpers
// =================================================================================================

uint8_t* read_file_bytes(const char* path, size_t* out_len) {
    FILE* f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t* buf = malloc(size);
    if (buf) fread(buf, 1, size, f);
    fclose(f);
    if (out_len) *out_len = size;
    return buf;
}

char* read_file_text(const char* path) {
    FILE* f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    char* buf = malloc(size + 1);
    if (buf) {
        fread(buf, 1, size, f);
        buf[size] = '\0';
    }
    fclose(f);
    return buf;
}

int write_file_bytes(const char* path, const uint8_t* data, size_t len) {
    FILE* f = fopen(path, "wb");
    if (!f) return 0;
    fwrite(data, 1, len, f);
    fclose(f);
    return 1;
}

int write_file_text(const char* path, const char* text) {
    FILE* f = fopen(path, "w");
    if (!f) return 0;
    fputs(text, f);
    fclose(f);
    return 1;
}

// =================================================================================================
// IL Helpers
// =================================================================================================

int load_il(const char* path, ILFile* il) {
    il->raw_data = read_file_bytes(path, &il->raw_len);
    if (!il->raw_data) return 0;
    
    if (il->raw_len < 16 || memcmp(il->raw_data, "CNDIL", 5) != 0) {
        printf("Invalid IL file magic\n");
        return 0;
    }
    
    il->str_count = *(uint16_t*)(il->raw_data + 6);
    uint32_t str_offset = *(uint32_t*)(il->raw_data + 8);
    uint32_t bc_offset = *(uint32_t*)(il->raw_data + 12);
    
    if (str_offset > il->raw_len || bc_offset > il->raw_len) return 0;

    il->string_table = malloc(il->str_count * sizeof(char*));
    const char* ptr = (const char*)(il->raw_data + str_offset);
    for (int i = 0; i < il->str_count; i++) {
        il->string_table[i] = ptr;
        ptr += strlen(ptr) + 1;
    }
    
    il->bytecode = il->raw_data + bc_offset;
    il->bytecode_len = il->raw_len - bc_offset;
    
    return 1;
}

void free_il(ILFile* il) {
    if (il->raw_data) free(il->raw_data);
    if (il->string_table) free(il->string_table);
}
