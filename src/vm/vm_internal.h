#ifndef VM_INTERNAL_H
#define VM_INTERNAL_H

#include "concordia.h"

// --- IL Access ---

static inline uint16_t read_il_u16(cnd_vm_ctx* ctx) {
    if (ctx->ip + 2 > ctx->program->bytecode_len) return 0; 
    uint16_t val = ctx->program->bytecode[ctx->ip] | (ctx->program->bytecode[ctx->ip + 1] << 8);
    ctx->ip += 2;
    return val;
}

static inline uint8_t read_il_u8(cnd_vm_ctx* ctx) {
    if (ctx->ip + 1 > ctx->program->bytecode_len) return 0; 
    uint8_t val = ctx->program->bytecode[ctx->ip];
    ctx->ip += 1;
    return val;
}

static inline uint32_t read_il_u32(cnd_vm_ctx* ctx) {
    if (ctx->ip + 4 > ctx->program->bytecode_len) return 0;
    uint32_t val = ctx->program->bytecode[ctx->ip] | 
                   (ctx->program->bytecode[ctx->ip + 1] << 8) | 
                   (ctx->program->bytecode[ctx->ip + 2] << 16) | 
                   (ctx->program->bytecode[ctx->ip + 3] << 24);
    ctx->ip += 4;
    return val;
}

static inline uint64_t read_il_u64(cnd_vm_ctx* ctx) {
    if (ctx->ip + 8 > ctx->program->bytecode_len) return 0;
    const uint8_t* b = &ctx->program->bytecode[ctx->ip];
    uint64_t val = ((uint64_t)b[0]) | 
                   ((uint64_t)b[1] << 8) | 
                   ((uint64_t)b[2] << 16) | 
                   ((uint64_t)b[3] << 24) |
                   ((uint64_t)b[4] << 32) | 
                   ((uint64_t)b[5] << 40) | 
                   ((uint64_t)b[6] << 48) | 
                   ((uint64_t)b[7] << 56);
    ctx->ip += 8;
    return val;
}

static inline uint32_t peek_il_u32(cnd_vm_ctx* ctx, size_t idx) {
    if (idx + 4 > ctx->program->bytecode_len) return 0;
    const uint8_t* b = ctx->program->bytecode + idx;
    return b[0] | (b[1] << 8) | (b[2] << 16) | (b[3] << 24);
}

// --- Data Access (Read) ---

static inline uint8_t read_u8(const uint8_t* buf) {
    return *buf;
}

static inline uint16_t read_u16(const uint8_t* buf, cnd_endian_t endian) {
    if (endian == CND_LE) {
        return buf[0] | (buf[1] << 8);
    } else {
        return (buf[0] << 8) | buf[1];
    }
}

static inline uint32_t read_u32(const uint8_t* buf, cnd_endian_t endian) {
    if (endian == CND_LE) {
        return buf[0] | (buf[1] << 8) | (buf[2] << 16) | (buf[3] << 24);
    } else {
        return (buf[0] << 24) | (buf[1] << 16) | (buf[2] << 8) | buf[3];
    }
}

static inline uint64_t read_u64(const uint8_t* buf, cnd_endian_t endian) {
    if (endian == CND_LE) {
        return ((uint64_t)buf[0]) | 
               ((uint64_t)buf[1] << 8) | 
               ((uint64_t)buf[2] << 16) | 
               ((uint64_t)buf[3] << 24) |
               ((uint64_t)buf[4] << 32) | 
               ((uint64_t)buf[5] << 40) | 
               ((uint64_t)buf[6] << 48) | 
               ((uint64_t)buf[7] << 56);
    } else {
        return ((uint64_t)buf[0] << 56) | 
               ((uint64_t)buf[1] << 48) | 
               ((uint64_t)buf[2] << 40) | 
               ((uint64_t)buf[3] << 32) |
               ((uint64_t)buf[4] << 24) | 
               ((uint64_t)buf[5] << 16) | 
               ((uint64_t)buf[6] << 8) | 
               ((uint64_t)buf[7]);
    }
}

static inline uint64_t read_bits(cnd_vm_ctx* ctx, uint8_t count) {
    // Optimization: Byte-aligned reads
    if (ctx->bit_offset == 0 && (count & 7) == 0) {
        uint8_t bytes = count / 8;
        if (ctx->cursor + bytes <= ctx->data_len) {
            if (bytes == 1) { uint64_t v = read_u8(ctx->data_buffer + ctx->cursor); ctx->cursor++; return v; }
            if (bytes == 2) { uint64_t v = read_u16(ctx->data_buffer + ctx->cursor, ctx->endianness); ctx->cursor+=2; return v; }
            if (bytes == 4) { uint64_t v = read_u32(ctx->data_buffer + ctx->cursor, ctx->endianness); ctx->cursor+=4; return v; }
            if (bytes == 8) { uint64_t v = read_u64(ctx->data_buffer + ctx->cursor, ctx->endianness); ctx->cursor+=8; return v; }
        }
    }

    uint64_t val = 0;
    for (uint8_t i = 0; i < count; i++) {
        if (ctx->cursor >= ctx->data_len) break; 

        uint8_t current_byte = ctx->data_buffer[ctx->cursor];
        uint8_t bit;

        if (ctx->endianness == CND_BE) {
            // BE: bit_offset 0 is MSB (7), 7 is LSB (0)
            // Read from MSB of stream, shift into LSB of val
            bit = (current_byte >> (7 - ctx->bit_offset)) & 1;
            val = (val << 1) | bit;
        } else {
            // LE: bit_offset 0 is LSB (0)
            // Read from LSB of stream, pack into LSB of val
            bit = (current_byte >> ctx->bit_offset) & 1;
            if (bit) val |= ((uint64_t)1 << i);
        }
        
        ctx->bit_offset++;
        if (ctx->bit_offset >= 8) {
            ctx->bit_offset = 0;
            ctx->cursor++;
        }
    }
    return val;
}

// --- Data Access (Write) ---

static inline void write_u8(uint8_t* buf, uint8_t val) {
    *buf = val;
}

static inline void write_u16(uint8_t* buf, uint16_t val, cnd_endian_t endian) {
    if (endian == CND_LE) {
        buf[0] = val & 0xFF;
        buf[1] = (val >> 8) & 0xFF;
    } else {
        buf[0] = (val >> 8) & 0xFF;
        buf[1] = val & 0xFF;
    }
}

static inline void write_u32(uint8_t* buf, uint32_t val, cnd_endian_t endian) {
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

static inline void write_u64(uint8_t* buf, uint64_t val, cnd_endian_t endian) {
    if (endian == CND_LE) {
        buf[0] = val & 0xFF;
        buf[1] = (val >> 8) & 0xFF;
        buf[2] = (val >> 16) & 0xFF;
        buf[3] = (val >> 24) & 0xFF;
        buf[4] = (val >> 32) & 0xFF;
        buf[5] = (val >> 40) & 0xFF;
        buf[6] = (val >> 48) & 0xFF;
        buf[7] = (val >> 56) & 0xFF;
    } else {
        buf[0] = (val >> 56) & 0xFF;
        buf[1] = (val >> 48) & 0xFF;
        buf[2] = (val >> 40) & 0xFF;
        buf[3] = (val >> 32) & 0xFF;
        buf[4] = (val >> 24) & 0xFF;
        buf[5] = (val >> 16) & 0xFF;
        buf[6] = (val >> 8) & 0xFF;
        buf[7] = val & 0xFF;
    }
}

static inline void write_bits(cnd_vm_ctx* ctx, uint64_t val, uint8_t count) {
    // Optimization: Byte-aligned writes
    if (ctx->bit_offset == 0 && (count & 7) == 0) {
        uint8_t bytes = count / 8;
        if (ctx->cursor + bytes <= ctx->data_len) {
            if (bytes == 1) { write_u8(ctx->data_buffer + ctx->cursor, (uint8_t)val); ctx->cursor++; return; }
            if (bytes == 2) { write_u16(ctx->data_buffer + ctx->cursor, (uint16_t)val, ctx->endianness); ctx->cursor+=2; return; }
            if (bytes == 4) { write_u32(ctx->data_buffer + ctx->cursor, (uint32_t)val, ctx->endianness); ctx->cursor+=4; return; }
            if (bytes == 8) { write_u64(ctx->data_buffer + ctx->cursor, val, ctx->endianness); ctx->cursor+=8; return; }
        }
    }

    for (uint8_t i = 0; i < count; i++) {
        if (ctx->cursor >= ctx->data_len) return; 

        uint8_t bit;
        uint8_t mask;

        if (ctx->endianness == CND_BE) {
            // BE: Write MSB of val first to MSB of stream
            bit = (val >> (count - 1 - i)) & 1;
            mask = 1 << (7 - ctx->bit_offset);
        } else {
            // LE: Write LSB of val first to LSB of stream
            bit = (val >> i) & 1;
            mask = 1 << ctx->bit_offset;
        }

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

#endif
