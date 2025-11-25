#include "concordia.h"
#include <string.h>

// --- Internal Helpers ---

static uint16_t read_il_u16(cnd_vm_ctx* ctx) {
    if (ctx->ip + 2 > ctx->il_len) return 0; 
    uint16_t val = ctx->il_code[ctx->ip] | (ctx->il_code[ctx->ip + 1] << 8);
    ctx->ip += 2;
    return val;
}

static uint8_t read_il_u8(cnd_vm_ctx* ctx) {
    if (ctx->ip + 1 > ctx->il_len) return 0; 
    uint8_t val = ctx->il_code[ctx->ip];
    ctx->ip += 1;
    return val;
}

static void write_u8(uint8_t* buf, uint8_t val) {
    *buf = val;
}

static uint8_t read_u8(const uint8_t* buf) {
    return *buf;
}

static void write_u16(uint8_t* buf, uint16_t val, cnd_endian_t endian) {
    if (endian == CND_LE) {
        buf[0] = val & 0xFF;
        buf[1] = (val >> 8) & 0xFF;
    } else {
        buf[0] = (val >> 8) & 0xFF;
        buf[1] = val & 0xFF;
    }
}

static uint16_t read_u16(const uint8_t* buf, cnd_endian_t endian) {
    if (endian == CND_LE) {
        return buf[0] | (buf[1] << 8);
    } else {
        return (buf[0] << 8) | buf[1];
    }
}

static void write_u32(uint8_t* buf, uint32_t val, cnd_endian_t endian) {
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

static uint32_t read_u32(const uint8_t* buf, cnd_endian_t endian) {
    if (endian == CND_LE) {
        return buf[0] | (buf[1] << 8) | (buf[2] << 16) | (buf[3] << 24);
    } else {
        return (buf[0] << 24) | (buf[1] << 16) | (buf[2] << 8) | buf[3];
    }
}

static void write_u64(uint8_t* buf, uint64_t val, cnd_endian_t endian) {
    if (endian == CND_LE) {
        for (int i = 0; i < 8; i++) buf[i] = (val >> (i * 8)) & 0xFF;
    } else {
        for (int i = 0; i < 8; i++) buf[i] = (val >> ((7 - i) * 8)) & 0xFF;
    }
}

static uint64_t read_u64(const uint8_t* buf, cnd_endian_t endian) {
    uint64_t val = 0;
    if (endian == CND_LE) {
        for (int i = 0; i < 8; i++) val |= ((uint64_t)buf[i] << (i * 8));
    } else {
        for (int i = 0; i < 8; i++) val |= ((uint64_t)buf[i] << ((7 - i) * 8));
    }
    return val;
}

static void write_bits(cnd_vm_ctx* ctx, uint64_t val, uint8_t count) {
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

static uint64_t read_bits(cnd_vm_ctx* ctx, uint8_t count) {
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

// --- Loop Stack Helpers ---

static void loop_push(cnd_vm_ctx* ctx, size_t start_ip, uint32_t count) {
    if (ctx->loop_depth >= CND_MAX_LOOP_DEPTH) return; // Error?
    ctx->loop_stack[ctx->loop_depth].start_ip = start_ip;
    ctx->loop_stack[ctx->loop_depth].remaining = count;
    ctx->loop_depth++;
}

static void loop_pop(cnd_vm_ctx* ctx) {
    if (ctx->loop_depth > 0) ctx->loop_depth--;
}

// --- Public API ---

void cnd_init(cnd_vm_ctx* ctx, 
              cnd_mode_t mode,
              const uint8_t* il, size_t il_len, 
              uint8_t* data, size_t data_len,
              cnd_io_cb cb, void* user) 
{
    if (!ctx) return;
    ctx->mode = mode;
    ctx->il_code = il;
    ctx->il_len = il_len;
    ctx->data_buffer = data;
    ctx->data_len = data_len;
    ctx->io_callback = cb;
    ctx->user_ptr = user;
    
    ctx->ip = 0;
    ctx->cursor = 0;
    ctx->bit_offset = 0;
    ctx->endianness = CND_LE; 
    ctx->loop_depth = 0;
}

cnd_error_t cnd_execute(cnd_vm_ctx* ctx) {
    if (!ctx || !ctx->il_code || !ctx->data_buffer) return CND_ERR_OOB;

    while (ctx->ip < ctx->il_len) {
        uint8_t opcode = ctx->il_code[ctx->ip];
        ctx->ip++; 

        // Check alignment
        if (opcode >= 0x10 && opcode < 0x20) { 
            if (ctx->bit_offset != 0) {
                ctx->cursor++;
                ctx->bit_offset = 0;
            }
        }
        // Category D (Strings/Arrays) also implies alignment
        if (opcode >= 0x30 && opcode < 0x40) {
             if (ctx->bit_offset != 0) {
                ctx->cursor++;
                ctx->bit_offset = 0;
            }
        }

        switch (opcode) {
            case OP_NOOP: break;
            case OP_SET_ENDIAN_LE: ctx->endianness = CND_LE; break;
            case OP_SET_ENDIAN_BE: ctx->endianness = CND_BE; break;
            
            case OP_ENTER_STRUCT: {
                uint16_t key = read_il_u16(ctx);
                if (ctx->io_callback(ctx, key, opcode, NULL) != CND_ERR_OK) return CND_ERR_CALLBACK;
                break;
            }
            
            case OP_EXIT_STRUCT: {
                if (ctx->io_callback(ctx, 0, opcode, NULL) != CND_ERR_OK) return CND_ERR_CALLBACK;
                break;
            }
            
            case OP_CONST_WRITE: {
                // Deprecated/Reserved in favor of Symmetric CONST_CHECK?
                // Or implementation specific. 
                // Let's implement as forced write.
                uint8_t type = read_il_u8(ctx);
                uint64_t val = 0;
                int size = 0;
                if (type == OP_IO_U8) { val = read_il_u8(ctx); size=1; }
                else if (type == OP_IO_U16) { val = read_il_u16(ctx); size=2; }
                else { return CND_ERR_INVALID_OP; }
                
                if (ctx->cursor + size > ctx->data_len) return CND_ERR_OOB;
                
                if (size == 1) write_u8(ctx->data_buffer + ctx->cursor, (uint8_t)val);
                else if (size == 2) write_u16(ctx->data_buffer + ctx->cursor, (uint16_t)val, ctx->endianness);
                
                ctx->cursor += size;
                break;
            }
            
            case OP_CONST_CHECK: {
                uint8_t type = read_il_u8(ctx);
                uint64_t expected = 0;
                int size = 0;
                
                if (type == OP_IO_U8) { expected = read_il_u8(ctx); size=1; }
                else if (type == OP_IO_U16) { expected = read_il_u16(ctx); size=2; }
                else { return CND_ERR_INVALID_OP; }
                
                if (ctx->cursor + size > ctx->data_len) return CND_ERR_OOB;
                
                if (ctx->mode == CND_MODE_ENCODE) {
                    // Write the constant
                    if (size == 1) write_u8(ctx->data_buffer + ctx->cursor, (uint8_t)expected);
                    else if (size == 2) write_u16(ctx->data_buffer + ctx->cursor, (uint16_t)expected, ctx->endianness);
                } else {
                    // Validate the constant
                    uint64_t actual = 0;
                    if (size == 1) actual = read_u8(ctx->data_buffer + ctx->cursor);
                    else if (size == 2) actual = read_u16(ctx->data_buffer + ctx->cursor, ctx->endianness);
                    if (actual != expected) return CND_ERR_VALIDATION;
                }
                
                ctx->cursor += size;
                break;
            }

            // ... Category B (Primitives) ...
            case OP_IO_U8: {
                uint16_t key = read_il_u16(ctx);
                if (ctx->cursor + 1 > ctx->data_len) return CND_ERR_OOB;
                uint8_t val = 0;
                if (ctx->mode == CND_MODE_ENCODE) {
                    if (ctx->io_callback(ctx, key, opcode, &val) != CND_ERR_OK) return CND_ERR_CALLBACK;
                    write_u8(ctx->data_buffer + ctx->cursor, val);
                } else {
                    val = read_u8(ctx->data_buffer + ctx->cursor);
                    if (ctx->io_callback(ctx, key, opcode, &val) != CND_ERR_OK) return CND_ERR_CALLBACK;
                }
                ctx->cursor += 1;
                break;
            }
            case OP_IO_U16: {
                uint16_t key = read_il_u16(ctx);
                if (ctx->cursor + 2 > ctx->data_len) return CND_ERR_OOB;
                uint16_t val = 0;
                if (ctx->mode == CND_MODE_ENCODE) {
                    if (ctx->io_callback(ctx, key, opcode, &val) != CND_ERR_OK) return CND_ERR_CALLBACK;
                    write_u16(ctx->data_buffer + ctx->cursor, val, ctx->endianness);
                } else {
                    val = read_u16(ctx->data_buffer + ctx->cursor, ctx->endianness);
                    if (ctx->io_callback(ctx, key, opcode, &val) != CND_ERR_OK) return CND_ERR_CALLBACK;
                }
                ctx->cursor += 2;
                break;
            }
            case OP_IO_U32: { uint16_t key = read_il_u16(ctx); if(ctx->cursor+4 > ctx->data_len) return CND_ERR_OOB; uint32_t val=0; if(ctx->mode==CND_MODE_ENCODE){ if(ctx->io_callback(ctx,key,opcode,&val)) return CND_ERR_CALLBACK; write_u32(ctx->data_buffer+ctx->cursor,val,ctx->endianness); } else { val=read_u32(ctx->data_buffer+ctx->cursor,ctx->endianness); if(ctx->io_callback(ctx,key,opcode,&val)) return CND_ERR_CALLBACK; } ctx->cursor+=4; break; }
            case OP_IO_F32: { uint16_t key = read_il_u16(ctx); if(ctx->cursor+4 > ctx->data_len) return CND_ERR_OOB; float val=0; if(ctx->mode==CND_MODE_ENCODE){ if(ctx->io_callback(ctx,key,opcode,&val)) return CND_ERR_CALLBACK; uint32_t t; memcpy(&t,&val,4); write_u32(ctx->data_buffer+ctx->cursor,t,ctx->endianness); } else { uint32_t t=read_u32(ctx->data_buffer+ctx->cursor,ctx->endianness); memcpy(&val,&t,4); if(ctx->io_callback(ctx,key,opcode,&val)) return CND_ERR_CALLBACK; } ctx->cursor+=4; break; }
            case OP_IO_F64: { uint16_t key = read_il_u16(ctx); if(ctx->cursor+8 > ctx->data_len) return CND_ERR_OOB; double val=0.0; if(ctx->mode==CND_MODE_ENCODE){ if(ctx->io_callback(ctx,key,opcode,&val)) return CND_ERR_CALLBACK; uint64_t t; memcpy(&t,&val,8); write_u64(ctx->data_buffer+ctx->cursor,t,ctx->endianness); } else { uint64_t t=read_u64(ctx->data_buffer+ctx->cursor,ctx->endianness); memcpy(&val,&t,8); if(ctx->io_callback(ctx,key,opcode,&val)) return CND_ERR_CALLBACK; } ctx->cursor+=8; break; }

            // ... Category C (Bitfields) ...
            case OP_IO_BIT_U: { uint16_t k=read_il_u16(ctx); uint8_t b=read_il_u8(ctx); uint64_t v=0; if(ctx->mode==CND_MODE_ENCODE){ if(ctx->io_callback(ctx,k,opcode,&v)) return CND_ERR_CALLBACK; write_bits(ctx,v,b); } else { v=read_bits(ctx,b); if(ctx->io_callback(ctx,k,opcode,&v)) return CND_ERR_CALLBACK; } break; }
            case OP_ALIGN_PAD: { uint8_t b=read_il_u8(ctx); for(int i=0;i<b;i++) { ctx->bit_offset++; if(ctx->bit_offset>=8){ctx->bit_offset=0; ctx->cursor++;} } break; }

            // --- Category D: Arrays & Strings ---
            
            case OP_STR_NULL: {
                uint16_t key = read_il_u16(ctx);
                uint16_t max_len = read_il_u16(ctx);
                
                if (ctx->mode == CND_MODE_ENCODE) {
                    // Callback gives char*
                    const char* str = NULL;
                    if (ctx->io_callback(ctx, key, opcode, &str) != CND_ERR_OK) return CND_ERR_CALLBACK;
                    
                    size_t len = str ? strlen(str) : 0;
                    if (len > max_len) len = max_len;
                    
                    if (ctx->cursor + len + 1 > ctx->data_len) return CND_ERR_OOB;
                    
                    memcpy(ctx->data_buffer + ctx->cursor, str, len);
                    ctx->cursor += len;
                    ctx->data_buffer[ctx->cursor] = 0x00; // Null terminator
                    ctx->cursor++;
                } else {
                    // Scan for null
                    size_t start = ctx->cursor;
                    size_t len = 0;
                    while (len < max_len && ctx->cursor < ctx->data_len) {
                        if (ctx->data_buffer[ctx->cursor] == 0x00) break;
                        ctx->cursor++;
                        len++;
                    }
                    if (ctx->cursor >= ctx->data_len) return CND_ERR_OOB; // Hit end of buffer before null
                    
                    // Pass string view to callback
                    const char* ptr = (const char*)(ctx->data_buffer + start);
                    if (ctx->io_callback(ctx, key, opcode, (void*)ptr) != CND_ERR_OK) return CND_ERR_CALLBACK;
                    
                    ctx->cursor++; // Skip null
                }
                break;
            }

            case OP_ARR_FIXED: {
                uint16_t count = read_il_u16(ctx);
                // Push Loop
                if (count > 0) {
                    loop_push(ctx, ctx->ip, count);
                } else {
                     loop_push(ctx, ctx->ip, count);
                     if (count == 0) {
                         int depth = 1;
                         while (ctx->ip < ctx->il_len && depth > 0) {
                             uint8_t op = ctx->il_code[ctx->ip];
                             if (op == OP_ARR_FIXED || op == OP_ARR_PRE_U8 || 
                                 op == OP_ARR_PRE_U16 || op == OP_ARR_PRE_U32) depth++;
                             if (op == OP_ARR_END) depth--;
                             ctx->ip++; 
                         }
                     }
                }
                break;
            }

            case OP_ARR_END: {
                if (ctx->loop_depth == 0) return CND_ERR_INVALID_OP;
                cnd_loop_frame* frame = &ctx->loop_stack[ctx->loop_depth - 1];
                
                if (frame->remaining > 0) frame->remaining--;
                
                if (frame->remaining > 0) {
                    ctx->ip = frame->start_ip;
                } else {
                    loop_pop(ctx);
                }
                break;
            }

            default:
                break;
        }
    }

    return CND_ERR_OK;
}
