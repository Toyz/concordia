#ifndef CLI_HELPERS_H
#define CLI_HELPERS_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <cJSON.h>
#include "concordia.h"
#include "compiler.h"

// --- Helper: File IO ---
uint8_t* read_file_bytes(const char* path, size_t* out_len);
char* read_file_text(const char* path);
int write_file_bytes(const char* path, const uint8_t* data, size_t len);
int write_file_text(const char* path, const char* text);

// --- Helper: IL Loader ---
typedef struct {
    uint8_t* raw_data;
    size_t raw_len;
    uint16_t str_count;
    const char** string_table; 
    const uint8_t* bytecode;
    size_t bytecode_len;
} ILFile;
int load_il(const char* path, ILFile* il);
void free_il(ILFile* il);

// --- VM IO Callback (JSON Binding) ---
typedef struct {
    ILFile* il;
    cJSON* stack[32]; // Stack for nested objects
    int depth;

    // For robust array handling
    cJSON* array_stack[32]; // Stack for current array cJSON object
    int array_index_stack[32]; // Stack for current index within that array
    int array_start_depth[32]; // Stack for io->depth when array started
    int array_depth; // Current depth in array stack
    
    int hex_mode; // Flag to enable hex string output for byte arrays

    // State for hex string accumulation for current byte array
    bool in_hex_byte_array;
    char* hex_str_buffer;
    size_t hex_str_buffer_len;
    size_t hex_str_buffer_capacity;
} IOCtx;
cnd_error_t json_io_callback(cnd_vm_ctx* ctx, uint16_t key_id, uint8_t type, void* ptr);


#endif
