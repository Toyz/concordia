#include "vm_internal.h"
#include <string.h> // For NULL if needed, though not used in IO functions typically

// --- IL Access ---

uint16_t read_il_u16(cnd_vm_ctx* ctx) {
    if (ctx->ip + 2 > ctx->il_len) return 0; 
    uint16_t val = ctx->il_code[ctx->ip] | (ctx->il_code[ctx->ip + 1] << 8);
    ctx->ip += 2;
    return val;
}

uint8_t read_il_u8(cnd_vm_ctx* ctx) {
    if (ctx->ip + 1 > ctx->il_len) return 0; 
    uint8_t val = ctx->il_code[ctx->ip];
    ctx->ip += 1;
    return val;
}

// --- Data Access (Read) ---

uint8_t read_u8(const uint8_t* buf) {
    return *buf;
}

uint16_t read_u16(const uint8_t* buf, cnd_endian_t endian) {
    if (endian == CND_LE) {
        return buf[0] | (buf[1] << 8);
    } else {
        return (buf[0] << 8) | buf[1];
    }
}

uint32_t read_u32(const uint8_t* buf, cnd_endian_t endian) {
    if (endian == CND_LE) {
        return buf[0] | (buf[1] << 8) | (buf[2] << 16) | (buf[3] << 24);
    } else {
        return (buf[0] << 24) | (buf[1] << 16) | (buf[2] << 8) | buf[3];
    }
}

uint64_t read_u64(const uint8_t* buf, cnd_endian_t endian) {
    uint64_t val = 0;
    if (endian == CND_LE) {
        for (int i = 0; i < 8; i++) val |= ((uint64_t)buf[i] << (i * 8));
    } else {
        for (int i = 0; i < 8; i++) val |= ((uint64_t)buf[i] << ((7 - i) * 8));
    }
    return val;
}

uint64_t read_bits(cnd_vm_ctx* ctx, uint8_t count) {
    uint64_t val = 0;
    for (uint8_t i = 0; i < count; i++) {
        if (ctx->cursor >= ctx->data_len) break; 

        uint8_t current_byte = ctx->data_buffer[ctx->cursor];
        uint8_t bit = (current_byte >> ctx->bit_offset) & 1;
        
        if (bit) val |= ((uint64_t)1 << i);
        
        ctx->bit_offset++;
        if (ctx->bit_offset >= 8) {
            ctx->bit_offset = 0;
            ctx->cursor++;
        }
    }
    return val;
}

// --- Data Access (Write) ---

void write_u8(uint8_t* buf, uint8_t val) {
    *buf = val;
}

void write_u16(uint8_t* buf, uint16_t val, cnd_endian_t endian) {
    if (endian == CND_LE) {
        buf[0] = val & 0xFF;
        buf[1] = (val >> 8) & 0xFF;
    } else {
        buf[0] = (val >> 8) & 0xFF;
        buf[1] = val & 0xFF;
    }
}

void write_u32(uint8_t* buf, uint32_t val, cnd_endian_t endian) {
    if (endian == CND_LE) {
        buf[0] = val & 0xFF;
        buf[1] = (val >> 8) & 0xFF;
        buf[2] = (val >> 16) & 0xFF;
        buf[3] = (val >> 24) & 0xFF;
    } else {
        buf[0] = (val >> 24) & 0xFF;
        buf[1] = (val >> 16) & 0xFF;
        buf[2] = (val >> 8) & 0xFF;
        buf[3] = val & 0xFF;
    }
}

void write_u64(uint8_t* buf, uint64_t val, cnd_endian_t endian) {
    if (endian == CND_LE) {
        for (int i = 0; i < 8; i++) buf[i] = (val >> (i * 8)) & 0xFF;
    } else {
        for (int i = 0; i < 8; i++) buf[i] = (val >> ((7 - i) * 8)) & 0xFF;
    }
}

void write_bits(cnd_vm_ctx* ctx, uint64_t val, uint8_t count) {
    for (uint8_t i = 0; i < count; i++) {
        if (ctx->cursor >= ctx->data_len) return; 

        uint8_t bit = (val >> i) & 1;
        uint8_t mask = 1 << ctx->bit_offset;
        uint8_t current_byte = ctx->data_buffer[ctx->cursor];
        
        if (bit) current_byte |= mask;
        else current_byte &= ~mask;
        
        ctx->data_buffer[ctx->cursor] = current_byte;
        
        ctx->bit_offset++;
        if (ctx->bit_offset >= 8) {
            ctx->bit_offset = 0;
            ctx->cursor++;
        }
    }
}
