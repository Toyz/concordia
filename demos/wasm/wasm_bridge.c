#include <emscripten.h>
#include <string.h>
#include <stdlib.h>
#include "concordia.h"

// --- JavaScript Imports ---
// These functions are implemented in JavaScript and called by C
extern void js_on_field(uint16_t key_id, uint8_t type, void* data_ptr);

// --- C Callback ---
// This bridges the VM callback to the JS world
cnd_error_t wasm_io_callback(cnd_vm_ctx* ctx, uint16_t key_id, uint8_t type, void* ptr) {
    // We pass the raw pointer to JS. 
    // JS can use Module.HEAPU8, HEAPU32, etc. to read the value at this address.
    js_on_field(key_id, type, ptr);
    return CND_ERR_OK;
}

// --- Global State ---
// In a real app, you might manage multiple contexts, but for a demo, one is fine.
cnd_program g_program;
cnd_vm_ctx g_ctx;
uint8_t* g_il_buffer = NULL;
uint8_t* g_data_buffer = NULL;

// --- Exports ---

EMSCRIPTEN_KEEPALIVE
void init_vm(uint8_t* il_data, int il_len) {
    if (g_il_buffer) free(g_il_buffer);
    g_il_buffer = malloc(il_len);
    memcpy(g_il_buffer, il_data, il_len);

    // Parse Header (Skip 12 bytes to find bytecode offset)
    // Header: Magic(5) Ver(1) StrCount(2) StrOffset(4) BytecodeOffset(4)
    uint32_t bytecode_offset = *(uint32_t*)(g_il_buffer + 12);
    
    cnd_program_load(&g_program, g_il_buffer + bytecode_offset, il_len - bytecode_offset);
}

EMSCRIPTEN_KEEPALIVE
int decode_packet(uint8_t* packet_data, int packet_len) {
    // Allocate buffer for the VM to read from
    if (g_data_buffer) free(g_data_buffer);
    g_data_buffer = malloc(packet_len);
    memcpy(g_data_buffer, packet_data, packet_len);

    // Initialize VM in DECODE mode
    cnd_init(&g_ctx, CND_MODE_DECODE, &g_program, g_data_buffer, packet_len, wasm_io_callback, NULL);
    
    // Run
    return cnd_execute(&g_ctx);
}

// Helper to allocate memory from JS
EMSCRIPTEN_KEEPALIVE
uint8_t* alloc_buffer(int size) {
    return malloc(size);
}

EMSCRIPTEN_KEEPALIVE
void free_buffer(uint8_t* ptr) {
    free(ptr);
}
