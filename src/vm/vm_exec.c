#include "vm_internal.h"
#include <string.h>
#include <stdio.h> // For debug prints

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
                // printf("VM_DEBUG: Calling callback for ENTER_STRUCT (Key %d)\n", key);
                if (ctx->io_callback(ctx, key, opcode, NULL) != CND_ERR_OK) return CND_ERR_CALLBACK;
                break; // VM continues with next instruction, which is the first field of the struct
            }
            
            case OP_EXIT_STRUCT: {
                // printf("VM_DEBUG: Calling callback for EXIT_STRUCT\n");
                if (ctx->io_callback(ctx, 0, opcode, NULL) != CND_ERR_OK) return CND_ERR_CALLBACK;
                break; // VM continues with next instruction after the struct
            }
            
            case OP_CONST_WRITE: {
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
                    if (size == 1) write_u8(ctx->data_buffer + ctx->cursor, (uint8_t)expected);
                    else if (size == 2) write_u16(ctx->data_buffer + ctx->cursor, (uint16_t)expected, ctx->endianness);
                } else {
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
                // printf("VM_DEBUG: Calling callback for IO_U8 (Key %d)\n", key);
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
                // printf("VM_DEBUG: Calling callback for IO_U16 (Key %d)\n", key);
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
            
            case OP_IO_I8: { uint16_t key = read_il_u16(ctx); if(ctx->cursor+1 > ctx->data_len) return CND_ERR_OOB; int8_t val=0; if(ctx->mode==CND_MODE_ENCODE){ if(ctx->io_callback(ctx,key,opcode,&val)) return CND_ERR_CALLBACK; write_u8(ctx->data_buffer+ctx->cursor,(uint8_t)val); } else { val=(int8_t)read_u8(ctx->data_buffer+ctx->cursor); if(ctx->io_callback(ctx,key,opcode,&val)) return CND_ERR_CALLBACK; } ctx->cursor+=1; break; }
            case OP_IO_I16: { uint16_t key = read_il_u16(ctx); if(ctx->cursor+2 > ctx->data_len) return CND_ERR_OOB; int16_t val=0; if(ctx->mode==CND_MODE_ENCODE){ if(ctx->io_callback(ctx,key,opcode,&val)) return CND_ERR_CALLBACK; write_u16(ctx->data_buffer+ctx->cursor,(uint16_t)val,ctx->endianness); } else { val=(int16_t)read_u16(ctx->data_buffer+ctx->cursor,ctx->endianness); if(ctx->io_callback(ctx,key,opcode,&val)) return CND_ERR_CALLBACK; } ctx->cursor+=2; break; }
            case OP_IO_I32: { uint16_t key = read_il_u16(ctx); if(ctx->cursor+4 > ctx->data_len) return CND_ERR_OOB; int32_t val=0; if(ctx->mode==CND_MODE_ENCODE){ if(ctx->io_callback(ctx,key,opcode,&val)) return CND_ERR_CALLBACK; write_u32(ctx->data_buffer+ctx->cursor,(uint32_t)val,ctx->endianness); } else { val=(int32_t)read_u32(ctx->data_buffer+ctx->cursor,ctx->endianness); if(ctx->io_callback(ctx,key,opcode,&val)) return CND_ERR_CALLBACK; } ctx->cursor+=4; break; }
            case OP_IO_I64: { uint16_t key = read_il_u16(ctx); if(ctx->cursor+8 > ctx->data_len) return CND_ERR_OOB; int64_t val=0; if(ctx->mode==CND_MODE_ENCODE){ if(ctx->io_callback(ctx,key,opcode,&val)) return CND_ERR_CALLBACK; write_u64(ctx->data_buffer+ctx->cursor,(uint64_t)val,ctx->endianness); } else { val=(int64_t)read_u64(ctx->data_buffer+ctx->cursor,ctx->endianness); if(ctx->io_callback(ctx,key,opcode,&val)) return CND_ERR_CALLBACK; } ctx->cursor+=8; break; }

            case OP_IO_F32: { uint16_t key = read_il_u16(ctx); if(ctx->cursor+4 > ctx->data_len) return CND_ERR_OOB; float val=0; if(ctx->mode==CND_MODE_ENCODE){ if(ctx->io_callback(ctx,key,opcode,&val)) return CND_ERR_CALLBACK; uint32_t t; memcpy(&t,&val,4); write_u32(ctx->data_buffer+ctx->cursor,t,ctx->endianness); } else { uint32_t t=read_u32(ctx->data_buffer+ctx->cursor,ctx->endianness); memcpy(&val,&t,4); if(ctx->io_callback(ctx,key,opcode,&val)) return CND_ERR_CALLBACK; } ctx->cursor+=4; break; } 
            case OP_IO_F64: { uint16_t key = read_il_u16(ctx); if(ctx->cursor+8 > ctx->data_len) return CND_ERR_OOB; double val=0.0; if(ctx->mode==CND_MODE_ENCODE){ if(ctx->io_callback(ctx,key,opcode,&val)) return CND_ERR_CALLBACK; uint64_t t; memcpy(&t,&val,8); write_u64(ctx->data_buffer+ctx->cursor,t,ctx->endianness); } else { uint64_t t=read_u64(ctx->data_buffer+ctx->cursor,ctx->endianness); memcpy(&val,&t,8); if(ctx->io_callback(ctx,key,opcode,&val)) return CND_ERR_CALLBACK; } ctx->cursor+=8; break; } 

            // ... Category C (Bitfields) ...
            case OP_IO_BIT_U: { uint16_t k=read_il_u16(ctx); uint8_t b=read_il_u8(ctx); uint64_t v=0; if(ctx->mode==CND_MODE_ENCODE){ if(ctx->io_callback(ctx,k,opcode,&v)) return CND_ERR_CALLBACK; write_bits(ctx,v,b); } else { v=read_bits(ctx,b); if(ctx->io_callback(ctx,k,opcode,&v)) return CND_ERR_CALLBACK; } break; } 
            case OP_ALIGN_PAD: { uint8_t b=read_il_u8(ctx); for(int i=0;i<b;i++) { ctx->bit_offset++; if(ctx->bit_offset>=8){ctx->bit_offset=0; ctx->cursor++;} } break; } 
            case OP_ALIGN_FILL: { 
                if (ctx->bit_offset != 0) {
                    // If encoding, we might want to ensure the remaining bits are 0.
                    // write_bits does RMW, so we should explicitly write 0s if we care.
                    // But simply advancing cursor and resetting bit_offset is enough if we assume
                    // the buffer is zero-initialized or we don't care about padding bits.
                    // However, for determinism, let's write 0s if encoding.
                    if (ctx->mode == CND_MODE_ENCODE) {
                        uint8_t bits_to_fill = 8 - ctx->bit_offset;
                        write_bits(ctx, 0, bits_to_fill); // This will advance cursor/bit_offset correctly
                    } else {
                        // For decoding, just skip
                        ctx->cursor++;
                        ctx->bit_offset = 0;
                    }
                }
                break; 
            } 

            // --- Category D: Arrays & Strings ---
            
            case OP_STR_NULL: {
                uint16_t key = read_il_u16(ctx);
                uint16_t max_len = read_il_u16(ctx);
                const char* str = NULL;
                // printf("VM_DEBUG: Calling callback for STR_NULL (Key %d)\n", key);
                if (ctx->mode == CND_MODE_ENCODE) {
                    if (ctx->io_callback(ctx, key, opcode, &str) != CND_ERR_OK) return CND_ERR_CALLBACK;
                    
                    size_t len = str ? strlen(str) : 0;
                    if (len > max_len) len = max_len;
                    
                    if (ctx->cursor + len + 1 > ctx->data_len) return CND_ERR_OOB;
                    
                    memcpy(ctx->data_buffer + ctx->cursor, str, len);
                    ctx->cursor += len;
                    ctx->data_buffer[ctx->cursor] = 0x00; // Null terminator
                    ctx->cursor++;
                } else {
                    size_t start = ctx->cursor;
                    size_t len = 0;
                    while (len < max_len && ctx->cursor < ctx->data_len) {
                        if (ctx->data_buffer[ctx->cursor] == 0x00) break;
                        ctx->cursor++;
                        len++;
                    }
                    if (ctx->cursor >= ctx->data_len) return CND_ERR_OOB; 
                    
                    const char* ptr = (const char*)(ctx->data_buffer + start);
                    if (ctx->io_callback(ctx, key, opcode, (void*)ptr) != CND_ERR_OK) return CND_ERR_CALLBACK;
                    
                    ctx->cursor++; // Skip null
                }
                break;
            }

            case OP_STR_PRE_U8: {
                uint16_t key = read_il_u16(ctx);
                const char* str = NULL;
                // printf("VM_DEBUG: Calling callback for STR_PRE_U8 (Key %d)\n", key);
                if (ctx->mode == CND_MODE_ENCODE) {
                    if (ctx->io_callback(ctx, key, opcode, &str) != CND_ERR_OK) return CND_ERR_CALLBACK;
                    size_t len = str ? strlen(str) : 0;
                    if (len > 255) len = 255;
                    if (ctx->cursor + 1 + len > ctx->data_len) return CND_ERR_OOB;
                    write_u8(ctx->data_buffer + ctx->cursor, (uint8_t)len);
                    memcpy(ctx->data_buffer + ctx->cursor + 1, str, len);
                    ctx->cursor += 1 + len;
                } else {
                    if (ctx->cursor + 1 > ctx->data_len) return CND_ERR_OOB;
                    uint8_t len = read_u8(ctx->data_buffer + ctx->cursor);
                    if (ctx->cursor + 1 + len > ctx->data_len) return CND_ERR_OOB;
                    const char* ptr = (const char*)(ctx->data_buffer + ctx->cursor + 1);
                    if (ctx->io_callback(ctx, key, opcode, (void*)ptr) != CND_ERR_OK) return CND_ERR_CALLBACK;
                    ctx->cursor += 1 + len;
                }
                break;
            }

            case OP_STR_PRE_U16: {
                uint16_t key = read_il_u16(ctx);
                const char* str = NULL;
                // printf("VM_DEBUG: Calling callback for STR_PRE_U16 (Key %d)\n", key);
                if (ctx->mode == CND_MODE_ENCODE) {
                    if (ctx->io_callback(ctx, key, opcode, &str) != CND_ERR_OK) return CND_ERR_CALLBACK;
                    size_t len = str ? strlen(str) : 0;
                    if (len > 65535) len = 65535;
                    if (ctx->cursor + 2 + len > ctx->data_len) return CND_ERR_OOB;
                    write_u16(ctx->data_buffer + ctx->cursor, (uint16_t)len, ctx->endianness);
                    memcpy(ctx->data_buffer + ctx->cursor + 2, str, len);
                    ctx->cursor += 2 + len;
                } else {
                    if (ctx->cursor + 2 > ctx->data_len) return CND_ERR_OOB;
                    uint16_t len = read_u16(ctx->data_buffer + ctx->cursor, ctx->endianness);
                    if (ctx->cursor + 2 + len > ctx->data_len) return CND_ERR_OOB;
                    const char* ptr = (const char*)(ctx->data_buffer + ctx->cursor + 2);
                    if (ctx->io_callback(ctx, key, opcode, (void*)ptr) != CND_ERR_OK) return CND_ERR_CALLBACK;
                    ctx->cursor += 2 + len;
                }
                break;
            }

            case OP_STR_PRE_U32: {
                uint16_t key = read_il_u16(ctx);
                const char* str = NULL;
                // printf("VM_DEBUG: Calling callback for STR_PRE_U32 (Key %d)\n", key);
                if (ctx->mode == CND_MODE_ENCODE) {
                    if (ctx->io_callback(ctx, key, opcode, &str) != CND_ERR_OK) return CND_ERR_CALLBACK;
                    size_t len = str ? strlen(str) : 0;
                    // u32 limit
                    if (ctx->cursor + 4 + len > ctx->data_len) return CND_ERR_OOB;
                    write_u32(ctx->data_buffer + ctx->cursor, (uint32_t)len, ctx->endianness);
                    memcpy(ctx->data_buffer + ctx->cursor + 4, str, len);
                    ctx->cursor += 4 + len;
                } else {
                    if (ctx->cursor + 4 > ctx->data_len) return CND_ERR_OOB;
                    uint32_t len = read_u32(ctx->data_buffer + ctx->cursor, ctx->endianness);
                    if (ctx->cursor + 4 + len > ctx->data_len) return CND_ERR_OOB;
                    const char* ptr = (const char*)(ctx->data_buffer + ctx->cursor + 4);
                    if (ctx->io_callback(ctx, key, opcode, (void*)ptr) != CND_ERR_OK) return CND_ERR_CALLBACK;
                    ctx->cursor += 4 + len;
                }
                break;
            }

            case OP_ARR_PRE_U8: {
                uint16_t key = read_il_u16(ctx);
                uint8_t count = 0;
                // printf("VM_DEBUG: Calling callback for ARR_PRE_U8 (Key %d)\n", key);
                if (ctx->mode == CND_MODE_ENCODE) {
                    if (ctx->io_callback(ctx, key, opcode, &count) != CND_ERR_OK) return CND_ERR_CALLBACK;
                    if (ctx->cursor + 1 > ctx->data_len) return CND_ERR_OOB;
                    write_u8(ctx->data_buffer + ctx->cursor, count);
                    ctx->cursor += 1;
                } else {
                    if (ctx->cursor + 1 > ctx->data_len) return CND_ERR_OOB;
                    count = read_u8(ctx->data_buffer + ctx->cursor);
                    ctx->cursor += 1;
                    if (ctx->io_callback(ctx, key, opcode, &count) != CND_ERR_OK) return CND_ERR_CALLBACK; // Report count to Host
                }
                if (count > 0) loop_push(ctx, ctx->ip, count);
                else { /* Handle 0 count loop (skip body) TODO */ }
                break;
            }

            case OP_ARR_PRE_U16: {
                uint16_t key = read_il_u16(ctx);
                uint16_t count = 0;
                // printf("VM_DEBUG: Calling callback for ARR_PRE_U16 (Key %d)\n", key);
                if (ctx->mode == CND_MODE_ENCODE) {
                    if (ctx->io_callback(ctx, key, opcode, &count) != CND_ERR_OK) return CND_ERR_CALLBACK;
                    if (ctx->cursor + 2 > ctx->data_len) return CND_ERR_OOB;
                    write_u16(ctx->data_buffer + ctx->cursor, count, ctx->endianness);
                    ctx->cursor += 2;
                } else {
                    if (ctx->cursor + 2 > ctx->data_len) return CND_ERR_OOB;
                    count = read_u16(ctx->data_buffer + ctx->cursor, ctx->endianness);
                    ctx->cursor += 2;
                    if (ctx->io_callback(ctx, key, opcode, &count) != CND_ERR_OK) return CND_ERR_CALLBACK; // Report count to Host
                }
                if (count > 0) loop_push(ctx, ctx->ip, count);
                else { /* Handle 0 count loop (skip body) TODO */ }
                break;
            }

            case OP_ARR_PRE_U32: {
                uint16_t key = read_il_u16(ctx);
                uint32_t count = 0;
                // printf("VM_DEBUG: Calling callback for ARR_PRE_U32 (Key %d)\n", key);
                if (ctx->mode == CND_MODE_ENCODE) {
                    if (ctx->io_callback(ctx, key, opcode, &count) != CND_ERR_OK) return CND_ERR_CALLBACK;
                    if (ctx->cursor + 4 > ctx->data_len) return CND_ERR_OOB;
                    write_u32(ctx->data_buffer + ctx->cursor, count, ctx->endianness);
                    ctx->cursor += 4;
                } else {
                    if (ctx->cursor + 4 > ctx->data_len) return CND_ERR_OOB;
                    count = read_u32(ctx->data_buffer + ctx->cursor, ctx->endianness);
                    ctx->cursor += 4;
                    if (ctx->io_callback(ctx, key, opcode, &count) != CND_ERR_OK) return CND_ERR_CALLBACK; // Report count to Host
                }
                if (count > 0) loop_push(ctx, ctx->ip, count);
                else { /* Handle 0 count loop (skip body) TODO */ }
                break;
            }

            case OP_ARR_FIXED: {
                uint16_t key = read_il_u16(ctx);
                uint16_t count = read_il_u16(ctx);
                // printf("VM_DEBUG: Calling callback for ARR_FIXED (Key %d)\n", key);
                if (ctx->mode == CND_MODE_ENCODE) {
                     // Notify host about array start so it can push context
                     if (ctx->io_callback(ctx, key, opcode, &count) != CND_ERR_OK) return CND_ERR_CALLBACK;
                } else {
                     // Notify host about array start
                     if (ctx->io_callback(ctx, key, opcode, &count) != CND_ERR_OK) return CND_ERR_CALLBACK;
                }

                if (count > 0) {
                    loop_push(ctx, ctx->ip, count);
                } else {
                     // Skip loop body if count is 0
                     int depth = 1;
                     while (ctx->ip < ctx->il_len && depth > 0) {
                         uint8_t op = ctx->il_code[ctx->ip];
                         if (op == OP_ARR_FIXED || op == OP_ARR_PRE_U8 || 
                             op == OP_ARR_PRE_U16 || op == OP_ARR_PRE_U32) depth++;
                         if (op == OP_ARR_END) depth--;
                         ctx->ip++; 
                     }
                }
                break;
            }

            case OP_ARR_END: {
                // printf("VM_DEBUG: Calling callback for ARR_END\n");
                if (ctx->loop_depth == 0) return CND_ERR_INVALID_OP;
                cnd_loop_frame* frame = &ctx->loop_stack[ctx->loop_depth - 1];
                
                if (frame->remaining > 0) frame->remaining--;
                
                if (frame->remaining > 0) {
                    ctx->ip = frame->start_ip;
                } else {
                    // Loop finished, notify callback
                    if (ctx->io_callback(ctx, 0, OP_ARR_END, NULL) != CND_ERR_OK) return CND_ERR_CALLBACK;
                    loop_pop(ctx);
                }
                break;
            }

            default:
                break;
        }
    }
    
    // fprintf(stderr, "VM_DEBUG: Execution finished. Cursor=%zu, DataLen=%zu. Returning OK.\n", ctx->cursor, ctx->data_len);
    return CND_ERR_OK;
}
