#include "vm_internal.h"
#include <string.h>
#include <stdio.h> // For debug prints

// --- Helpers / Macros to reduce repeated IO case code ---
// HANDLE_PRIMITIVE(size, ctype, READ_EXPR, WRITE_EXPR)
//   - size: number of bytes this IO consumes
//   - ctype: C type used for the value (e.g. uint8_t)
//   - READ_EXPR: expression to evaluate to read `ctype` from data buffer
//   - WRITE_EXPR: expression to evaluate to write `val` into buffer
#define HANDLE_PRIMITIVE(size, ctype, READ_EXPR, WRITE_EXPR) \
    { uint16_t key = read_il_u16(ctx); \
      if (ctx->cursor + (size) > ctx->data_len) { \
          if (ctx->is_next_optional) { \
              ctx->is_next_optional = false; \
              ctype val = 0; \
              if (ctx->io_callback(ctx, key, opcode, &val) != CND_ERR_OK) return CND_ERR_CALLBACK; \
              break; \
          } \
          return CND_ERR_OOB; \
      } \
      if (ctx->trans_type != CND_TRANS_NONE) { \
          if (ctx->trans_type == CND_TRANS_SCALE_F64) { \
              double eng_val = 0; \
              if (ctx->mode == CND_MODE_ENCODE) { \
                  if (ctx->io_callback(ctx, key, OP_IO_F64, &eng_val) != CND_ERR_OK) return CND_ERR_CALLBACK; \
                  ctype val = (ctype)((eng_val - ctx->trans_f_offset) / ctx->trans_f_factor); \
                  WRITE_EXPR; \
              } else { \
                  ctype raw = (READ_EXPR); \
                  eng_val = (double)raw * ctx->trans_f_factor + ctx->trans_f_offset; \
                  if (ctx->io_callback(ctx, key, OP_IO_F64, &eng_val) != CND_ERR_OK) return CND_ERR_CALLBACK; \
              } \
          } else { \
              int64_t eng_val = 0; \
              if (ctx->mode == CND_MODE_ENCODE) { \
                  if (ctx->io_callback(ctx, key, OP_IO_I64, &eng_val) != CND_ERR_OK) return CND_ERR_CALLBACK; \
                  int64_t raw64 = eng_val; \
                  switch(ctx->trans_type) { \
                      case CND_TRANS_ADD_I64: raw64 -= ctx->trans_i_val; break; \
                      case CND_TRANS_SUB_I64: raw64 += ctx->trans_i_val; break; \
                      case CND_TRANS_MUL_I64: if(ctx->trans_i_val!=0) raw64 /= ctx->trans_i_val; break; \
                      case CND_TRANS_DIV_I64: raw64 *= ctx->trans_i_val; break; \
                      default: break; \
                  } \
                  ctype val = (ctype)raw64; \
                  WRITE_EXPR; \
              } else { \
                  ctype raw = (READ_EXPR); \
                  int64_t raw64 = (int64_t)raw; \
                  switch(ctx->trans_type) { \
                      case CND_TRANS_ADD_I64: raw64 += ctx->trans_i_val; break; \
                      case CND_TRANS_SUB_I64: raw64 -= ctx->trans_i_val; break; \
                      case CND_TRANS_MUL_I64: raw64 *= ctx->trans_i_val; break; \
                      case CND_TRANS_DIV_I64: if(ctx->trans_i_val!=0) raw64 /= ctx->trans_i_val; break; \
                      default: break; \
                  } \
                  eng_val = raw64; \
                  if (ctx->io_callback(ctx, key, OP_IO_I64, &eng_val) != CND_ERR_OK) return CND_ERR_CALLBACK; \
              } \
          } \
          ctx->trans_type = CND_TRANS_NONE; \
      } else { \
          ctype val = 0; \
          if (ctx->mode == CND_MODE_ENCODE) { \
              if (ctx->io_callback(ctx, key, opcode, &val) != CND_ERR_OK) return CND_ERR_CALLBACK; \
              WRITE_EXPR; \
          } else { \
              val = (READ_EXPR); \
              if (ctx->io_callback(ctx, key, opcode, &val) != CND_ERR_OK) return CND_ERR_CALLBACK; \
          } \
      } \
      ctx->cursor += (size); \
      ctx->is_next_optional = false; \
      break; }

// Helper for float/double where we must memcopy to/from an integer representation
#define HANDLE_FLOAT(size, ctype, int_t, READ_INT_EXPR, WRITE_INT_EXPR) \
    { uint16_t key = read_il_u16(ctx); \
      if (ctx->cursor + (size) > ctx->data_len) { \
          if (ctx->is_next_optional) { \
              ctx->is_next_optional = false; \
              double val = 0; \
              /* Assuming callback handles F64/F32 via OP_IO_... */ \
              /* If F32, we pass float pointer? No, test callback uses casts. */ \
              /* Let's zero init and pass pointer. */ \
              if (ctx->io_callback(ctx, key, opcode, &val) != CND_ERR_OK) return CND_ERR_CALLBACK; \
              break; \
          } \
          return CND_ERR_OOB; \
      } \
      if (ctx->trans_type != CND_TRANS_NONE) { \
          if (ctx->trans_type == CND_TRANS_SCALE_F64) { \
              double eng_val = 0; \
              if (ctx->mode == CND_MODE_ENCODE) { \
                  if (ctx->io_callback(ctx, key, OP_IO_F64, &eng_val) != CND_ERR_OK) return CND_ERR_CALLBACK; \
                  ctype val = (ctype)((eng_val - ctx->trans_f_offset) / ctx->trans_f_factor); \
                  int_t t; memcpy(&t, &val, sizeof(t)); WRITE_INT_EXPR; \
              } else { \
                  int_t t = (READ_INT_EXPR); ctype val; memcpy(&val, &t, sizeof(t)); \
                  eng_val = (double)val * ctx->trans_f_factor + ctx->trans_f_offset; \
                  if (ctx->io_callback(ctx, key, OP_IO_F64, &eng_val) != CND_ERR_OK) return CND_ERR_CALLBACK; \
              } \
          } else { \
              int64_t eng_val = 0; \
              if (ctx->mode == CND_MODE_ENCODE) { \
                  if (ctx->io_callback(ctx, key, OP_IO_I64, &eng_val) != CND_ERR_OK) return CND_ERR_CALLBACK; \
                  int64_t raw64 = eng_val; \
                  switch(ctx->trans_type) { \
                      case CND_TRANS_ADD_I64: raw64 -= ctx->trans_i_val; break; \
                      case CND_TRANS_SUB_I64: raw64 += ctx->trans_i_val; break; \
                      case CND_TRANS_MUL_I64: if(ctx->trans_i_val!=0) raw64 /= ctx->trans_i_val; break; \
                      case CND_TRANS_DIV_I64: raw64 *= ctx->trans_i_val; break; \
                      default: break; \
                  } \
                  ctype val = (ctype)raw64; \
                  int_t t; memcpy(&t, &val, sizeof(t)); WRITE_INT_EXPR; \
              } else { \
                  int_t t = (READ_INT_EXPR); ctype val; memcpy(&val, &t, sizeof(t)); \
                  int64_t raw64 = (int64_t)val; \
                  switch(ctx->trans_type) { \
                      case CND_TRANS_ADD_I64: raw64 += ctx->trans_i_val; break; \
                      case CND_TRANS_SUB_I64: raw64 -= ctx->trans_i_val; break; \
                      case CND_TRANS_MUL_I64: raw64 *= ctx->trans_i_val; break; \
                      case CND_TRANS_DIV_I64: if(ctx->trans_i_val!=0) raw64 /= ctx->trans_i_val; break; \
                      default: break; \
                  } \
                  eng_val = raw64; \
                  if (ctx->io_callback(ctx, key, OP_IO_I64, &eng_val) != CND_ERR_OK) return CND_ERR_CALLBACK; \
              } \
          } \
          ctx->trans_type = CND_TRANS_NONE; \
      } else { \
          ctype val = 0; \
          if (ctx->mode == CND_MODE_ENCODE) { \
              if (ctx->io_callback(ctx, key, opcode, &val) != CND_ERR_OK) return CND_ERR_CALLBACK; \
              int_t t; memcpy(&t, &val, sizeof(t)); WRITE_INT_EXPR; \
          } else { \
              int_t t = (READ_INT_EXPR); memcpy(&val, &t, sizeof(t)); \
              if (ctx->io_callback(ctx, key, opcode, &val) != CND_ERR_OK) return CND_ERR_CALLBACK; \
          } \
      } \
      ctx->cursor += (size); \
      ctx->is_next_optional = false; \
      break; }

#define CHECK_RANGE(size, ctype, IL_READ, BUF_READ) \
    { \
        ctype min = (ctype)(IL_READ); \
        ctype max = (ctype)(IL_READ); \
        if (ctx->cursor < size) return CND_ERR_OOB; \
        ctype val = (ctype)(BUF_READ); \
        if (val < min || val > max) return CND_ERR_VALIDATION; \
    }

#define CHECK_RANGE_FLOAT(size, ctype, int_type, IL_READ, BUF_READ) \
    { \
        int_type imin = (IL_READ); \
        int_type imax = (IL_READ); \
        ctype min, max; \
        memcpy(&min, &imin, sizeof(ctype)); \
        memcpy(&max, &imax, sizeof(ctype)); \
        if (ctx->cursor < size) return CND_ERR_OOB; \
        int_type ival = (BUF_READ); \
        ctype val; \
        memcpy(&val, &ival, sizeof(ctype)); \
        if (val < min || val > max) return CND_ERR_VALIDATION; \
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

static int64_t sign_extend(uint64_t val, uint8_t bits) {
    if (bits >= 64) return (int64_t)val;
    uint64_t m = 1ULL << (bits - 1);
    return (int64_t)((val ^ m) - m);
}

// --- CRC Helpers ---

static uint32_t reflect(uint32_t val, int bits) {
    uint32_t res = 0;
    for (int i = 0; i < bits; i++) {
        if (val & (1 << i)) res |= (1 << (bits - 1 - i));
    }
    return res;
}

static uint32_t calc_crc(const uint8_t* data, size_t len, uint32_t poly, uint32_t init, uint32_t xorout, uint8_t flags, int width) {
    uint32_t crc = init;
    bool refin = flags & 1;
    bool refout = flags & 2;
    uint32_t mask = (width == 32) ? 0xFFFFFFFF : 0xFFFF;

    for (size_t i = 0; i < len; i++) {
        uint8_t octet = data[i];
        if (refin) octet = (uint8_t)reflect(octet, 8);
        
        if (width == 16) {
            crc ^= (uint16_t)(octet << 8);
            for (int j = 0; j < 8; j++) {
                if (crc & 0x8000) crc = ((crc << 1) ^ poly) & 0xFFFF;
                else crc = (crc << 1) & 0xFFFF;
            }
        } else { // 32
            crc ^= (uint32_t)(octet << 24);
            for (int j = 0; j < 8; j++) {
                if (crc & 0x80000000) crc = (crc << 1) ^ poly;
                else crc <<= 1;
            }
        }
    }
    
    if (refout) crc = reflect(crc, width);
    return (crc ^ xorout) & mask;
}

static bool should_align(uint8_t opcode) {
    // Category A: Meta & State (0x00 - 0x0F) - No alignment
    if (opcode < 0x10) return false;
    
    // Category B: Primitives (0x10 - 0x1F) - Align
    if (opcode < 0x20) return true;
    
    // Category C: Bitfields (0x20 - 0x2F) - No alignment
    if (opcode < 0x30) return false;
    
    // Category D: Arrays & Strings (0x30 - 0x3F) - Align
    if (opcode < 0x40) return true;
    
    // Category E: Validation (0x40 - 0x4F)
    // Exceptions that do NOT align:
    if (opcode == OP_RANGE_CHECK || 
        opcode == OP_SCALE_LIN || 
        opcode == OP_TRANS_ADD || 
        opcode == OP_TRANS_SUB || 
        opcode == OP_TRANS_MUL || 
        opcode == OP_TRANS_DIV || 
        opcode == OP_MARK_OPTIONAL) return false;
        
    // Category F: Control Flow (0x50 - 0x5F) - No alignment
    if (opcode >= 0x50) return false;
    
    // Default for remaining Category E (CRC, Const) is true
    return true;
}

// --- Public API ---

void cnd_program_load(cnd_program* program, const uint8_t* bytecode, size_t len) {
    if (!program) return;
    program->bytecode = bytecode;
    program->bytecode_len = len;
}

void cnd_init(cnd_vm_ctx* ctx, 
              cnd_mode_t mode,
              const cnd_program* program,
              uint8_t* data, size_t data_len,
              cnd_io_cb cb, void* user) 
{
    if (!ctx) return;
    ctx->mode = mode;
    ctx->program = program;
    ctx->data_buffer = data;
    ctx->data_len = data_len;
    ctx->io_callback = cb;
    ctx->user_ptr = user;
    
    ctx->ip = 0;
    ctx->cursor = 0;
    ctx->bit_offset = 0;
    ctx->endianness = CND_LE; 
    ctx->loop_depth = 0;
    
    ctx->trans_type = CND_TRANS_NONE;
    ctx->trans_f_factor = 1.0;
    ctx->trans_f_offset = 0.0;
    ctx->trans_i_val = 0;
    
    ctx->is_next_optional = false;
}

cnd_error_t cnd_execute(cnd_vm_ctx* ctx) {
    if (!ctx || !ctx->program || !ctx->program->bytecode || !ctx->data_buffer) return CND_ERR_OOB;

    while (ctx->ip < ctx->program->bytecode_len) {
        uint8_t opcode = ctx->program->bytecode[ctx->ip];
        ctx->ip++; 

        // Check alignment
        if (should_align(opcode)) {
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
                else if (type == OP_IO_U32) { val = read_il_u32(ctx); size=4; }
                else if (type == OP_IO_U64) { val = read_il_u64(ctx); size=8; }
                else { return CND_ERR_INVALID_OP; }
                
                if (ctx->cursor + size > ctx->data_len) return CND_ERR_OOB;
                
                if (size == 1) write_u8(ctx->data_buffer + ctx->cursor, (uint8_t)val);
                else if (size == 2) write_u16(ctx->data_buffer + ctx->cursor, (uint16_t)val, ctx->endianness);
                else if (size == 4) write_u32(ctx->data_buffer + ctx->cursor, (uint32_t)val, ctx->endianness);
                else if (size == 8) write_u64(ctx->data_buffer + ctx->cursor, val, ctx->endianness);
                
                ctx->cursor += size;
                break;
            }
            
            case OP_CONST_CHECK: {
                uint8_t type = read_il_u8(ctx);
                uint64_t expected = 0;
                int size = 0;
                if (type == OP_IO_U8) { expected = read_il_u8(ctx); size=1; }
                else if (type == OP_IO_U16) { expected = read_il_u16(ctx); size=2; }
                else if (type == OP_IO_U32) { expected = read_il_u32(ctx); size=4; }
                else if (type == OP_IO_U64) { expected = read_il_u64(ctx); size=8; }
                else { return CND_ERR_INVALID_OP; }
                
                if (ctx->cursor + size > ctx->data_len) return CND_ERR_OOB;
                
                if (ctx->mode == CND_MODE_ENCODE) {
                    if (size == 1) write_u8(ctx->data_buffer + ctx->cursor, (uint8_t)expected);
                    else if (size == 2) write_u16(ctx->data_buffer + ctx->cursor, (uint16_t)expected, ctx->endianness);
                    else if (size == 4) write_u32(ctx->data_buffer + ctx->cursor, (uint32_t)expected, ctx->endianness);
                    else if (size == 8) write_u64(ctx->data_buffer + ctx->cursor, expected, ctx->endianness);
                } else {
                    uint64_t actual = 0;
                    if (size == 1) actual = read_u8(ctx->data_buffer + ctx->cursor);
                    else if (size == 2) actual = read_u16(ctx->data_buffer + ctx->cursor, ctx->endianness);
                    else if (size == 4) actual = read_u32(ctx->data_buffer + ctx->cursor, ctx->endianness);
                    else if (size == 8) actual = read_u64(ctx->data_buffer + ctx->cursor, ctx->endianness);
                    if (actual != expected) return CND_ERR_VALIDATION;
                }
                
                ctx->cursor += size;
                break;
            }

            case OP_RANGE_CHECK: {
                uint8_t type = read_il_u8(ctx);
                switch (type) {
                    case OP_IO_U8:  CHECK_RANGE(1, uint8_t, read_il_u8(ctx), read_u8(ctx->data_buffer + ctx->cursor - 1)); break;
                    case OP_IO_I8:  CHECK_RANGE(1, int8_t,  read_il_u8(ctx), read_u8(ctx->data_buffer + ctx->cursor - 1)); break;
                    case OP_IO_U16: CHECK_RANGE(2, uint16_t, read_il_u16(ctx), read_u16(ctx->data_buffer + ctx->cursor - 2, ctx->endianness)); break;
                    case OP_IO_I16: CHECK_RANGE(2, int16_t,  read_il_u16(ctx), read_u16(ctx->data_buffer + ctx->cursor - 2, ctx->endianness)); break;
                    case OP_IO_U32: CHECK_RANGE(4, uint32_t, read_il_u32(ctx), read_u32(ctx->data_buffer + ctx->cursor - 4, ctx->endianness)); break;
                    case OP_IO_I32: CHECK_RANGE(4, int32_t,  read_il_u32(ctx), read_u32(ctx->data_buffer + ctx->cursor - 4, ctx->endianness)); break;
                    case OP_IO_U64: CHECK_RANGE(8, uint64_t, read_il_u64(ctx), read_u64(ctx->data_buffer + ctx->cursor - 8, ctx->endianness)); break;
                    case OP_IO_I64: CHECK_RANGE(8, int64_t,  read_il_u64(ctx), read_u64(ctx->data_buffer + ctx->cursor - 8, ctx->endianness)); break;
                    
                    case OP_IO_F32: CHECK_RANGE_FLOAT(4, float, uint32_t, read_il_u32(ctx), read_u32(ctx->data_buffer + ctx->cursor - 4, ctx->endianness)); break;
                    case OP_IO_F64: CHECK_RANGE_FLOAT(8, double, uint64_t, read_il_u64(ctx), read_u64(ctx->data_buffer + ctx->cursor - 8, ctx->endianness)); break;
                    
                    default: return CND_ERR_INVALID_OP;
                }
                break;
            }

            case OP_CRC_16: {
                uint16_t poly = read_il_u16(ctx);
                uint16_t init = read_il_u16(ctx);
                uint16_t xorout = read_il_u16(ctx);
                uint8_t flags = read_il_u8(ctx);
                
                uint32_t crc = calc_crc(ctx->data_buffer, ctx->cursor, poly, init, xorout, flags, 16);
                
                if (ctx->cursor + 2 > ctx->data_len) return CND_ERR_OOB;
                
                if (ctx->mode == CND_MODE_ENCODE) {
                    write_u16(ctx->data_buffer + ctx->cursor, (uint16_t)crc, ctx->endianness);
                } else {
                    uint16_t actual = read_u16(ctx->data_buffer + ctx->cursor, ctx->endianness);
                    if (actual != (uint16_t)crc) return CND_ERR_VALIDATION;
                }
                ctx->cursor += 2;
                break;
            }

            case OP_CRC_32: {
                uint32_t poly = read_il_u32(ctx);
                uint32_t init = read_il_u32(ctx);
                uint32_t xorout = read_il_u32(ctx);
                uint8_t flags = read_il_u8(ctx);
                
                uint32_t crc = calc_crc(ctx->data_buffer, ctx->cursor, poly, init, xorout, flags, 32);
                
                if (ctx->cursor + 4 > ctx->data_len) return CND_ERR_OOB;
                
                if (ctx->mode == CND_MODE_ENCODE) {
                    write_u32(ctx->data_buffer + ctx->cursor, crc, ctx->endianness);
                } else {
                    uint32_t actual = read_u32(ctx->data_buffer + ctx->cursor, ctx->endianness);
                    if (actual != crc) return CND_ERR_VALIDATION;
                }
                ctx->cursor += 4;
                break;
            }

            case OP_SCALE_LIN: {
                uint64_t i_fac = read_il_u64(ctx);
                uint64_t i_off = read_il_u64(ctx);
                double fac, off;
                memcpy(&fac, &i_fac, 8);
                memcpy(&off, &i_off, 8);
                ctx->trans_type = CND_TRANS_SCALE_F64;
                ctx->trans_f_factor = fac;
                ctx->trans_f_offset = off;
                break;
            }

            case OP_MARK_OPTIONAL:
                ctx->is_next_optional = true;
                break;
            
            case OP_TRANS_ADD: ctx->trans_type = CND_TRANS_ADD_I64; ctx->trans_i_val = (int64_t)read_il_u64(ctx); break;
            case OP_TRANS_SUB: ctx->trans_type = CND_TRANS_SUB_I64; ctx->trans_i_val = (int64_t)read_il_u64(ctx); break;
            case OP_TRANS_MUL: ctx->trans_type = CND_TRANS_MUL_I64; ctx->trans_i_val = (int64_t)read_il_u64(ctx); break;
            case OP_TRANS_DIV: ctx->trans_type = CND_TRANS_DIV_I64; ctx->trans_i_val = (int64_t)read_il_u64(ctx); break;

            // ... Category B (Primitives) ...
            case OP_IO_U8: HANDLE_PRIMITIVE(1, uint8_t, read_u8(ctx->data_buffer + ctx->cursor), write_u8(ctx->data_buffer + ctx->cursor, val));
            case OP_IO_U16: HANDLE_PRIMITIVE(2, uint16_t, read_u16(ctx->data_buffer + ctx->cursor, ctx->endianness), write_u16(ctx->data_buffer + ctx->cursor, val, ctx->endianness));
            case OP_IO_U32: HANDLE_PRIMITIVE(4, uint32_t, read_u32(ctx->data_buffer + ctx->cursor, ctx->endianness), write_u32(ctx->data_buffer + ctx->cursor, val, ctx->endianness));
            case OP_IO_U64: HANDLE_PRIMITIVE(8, uint64_t, read_u64(ctx->data_buffer + ctx->cursor, ctx->endianness), write_u64(ctx->data_buffer + ctx->cursor, val, ctx->endianness));
            
            case OP_IO_I8: HANDLE_PRIMITIVE(1, int8_t, (int8_t)read_u8(ctx->data_buffer + ctx->cursor), write_u8(ctx->data_buffer + ctx->cursor, (uint8_t)val));
            case OP_IO_I16: HANDLE_PRIMITIVE(2, int16_t, (int16_t)read_u16(ctx->data_buffer + ctx->cursor, ctx->endianness), write_u16(ctx->data_buffer + ctx->cursor, (uint16_t)val, ctx->endianness));
            case OP_IO_I32: HANDLE_PRIMITIVE(4, int32_t, (int32_t)read_u32(ctx->data_buffer + ctx->cursor, ctx->endianness), write_u32(ctx->data_buffer + ctx->cursor, (uint32_t)val, ctx->endianness));
            case OP_IO_I64: HANDLE_PRIMITIVE(8, int64_t, (int64_t)read_u64(ctx->data_buffer + ctx->cursor, ctx->endianness), write_u64(ctx->data_buffer + ctx->cursor, (uint64_t)val, ctx->endianness));

            case OP_IO_F32: HANDLE_FLOAT(4, float, uint32_t, read_u32(ctx->data_buffer + ctx->cursor, ctx->endianness), write_u32(ctx->data_buffer + ctx->cursor, t, ctx->endianness));
            case OP_IO_F64: HANDLE_FLOAT(8, double, uint64_t, read_u64(ctx->data_buffer + ctx->cursor, ctx->endianness), write_u64(ctx->data_buffer + ctx->cursor, t, ctx->endianness));

            // ... Category C (Bitfields) ...
            case OP_IO_BIT_U: { uint16_t k=read_il_u16(ctx); uint8_t b=read_il_u8(ctx); uint64_t v=0; if(ctx->mode==CND_MODE_ENCODE){ if(ctx->io_callback(ctx,k,opcode,&v)) return CND_ERR_CALLBACK; write_bits(ctx,v,b); } else { v=read_bits(ctx,b); if(ctx->io_callback(ctx,k,opcode,&v)) return CND_ERR_CALLBACK; } break; } 
            case OP_IO_BIT_I: { 
                uint16_t k=read_il_u16(ctx); 
                uint8_t b=read_il_u8(ctx); 
                int64_t v=0; 
                if(ctx->mode==CND_MODE_ENCODE){ 
                    if(ctx->io_callback(ctx,k,opcode,&v)) return CND_ERR_CALLBACK; 
                    write_bits(ctx,(uint64_t)v,b); 
                } else { 
                    uint64_t raw = read_bits(ctx,b); 
                    v = sign_extend(raw, b);
                    if(ctx->io_callback(ctx,k,opcode,&v)) return CND_ERR_CALLBACK; 
                } 
                break; 
            }
            case OP_ALIGN_PAD: { uint8_t b=read_il_u8(ctx); for(int i=0;i<b;i++) { ctx->bit_offset++; if(ctx->bit_offset>=8){ctx->bit_offset=0; ctx->cursor++;} } break; } 
            case OP_ALIGN_FILL: { 
                uint8_t fill_bit = read_il_u8(ctx);
                if (ctx->bit_offset != 0) {
                    if (ctx->mode == CND_MODE_ENCODE) {
                        uint8_t bits_to_fill = 8 - ctx->bit_offset;
                        uint64_t fill_val = (fill_bit) ? ((1ULL << bits_to_fill) - 1) : 0;
                        write_bits(ctx, fill_val, bits_to_fill); 
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
                    if (ctx->io_callback(ctx, key, opcode, &str) != CND_ERR_OK) {
                        if (ctx->is_next_optional) {
                            ctx->is_next_optional = false;
                            break; // Skip optional string
                        }
                        return CND_ERR_CALLBACK;
                    }
                    
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
                ctx->is_next_optional = false;
                break;
            }

            case OP_STR_PRE_U8: {
                uint16_t key = read_il_u16(ctx);
                const char* str = NULL;
                // printf("VM_DEBUG: Calling callback for STR_PRE_U8 (Key %d)\n", key);
                if (ctx->mode == CND_MODE_ENCODE) {
                    if (ctx->io_callback(ctx, key, opcode, &str) != CND_ERR_OK) {
                        if (ctx->is_next_optional) {
                            ctx->is_next_optional = false;
                            break;
                        }
                        return CND_ERR_CALLBACK;
                    }
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
                ctx->is_next_optional = false;
                break;
            }

            case OP_STR_PRE_U16: {
                uint16_t key = read_il_u16(ctx);
                const char* str = NULL;
                // printf("VM_DEBUG: Calling callback for STR_PRE_U16 (Key %d)\n", key);
                if (ctx->mode == CND_MODE_ENCODE) {
                    if (ctx->io_callback(ctx, key, opcode, &str) != CND_ERR_OK) {
                        if (ctx->is_next_optional) {
                            ctx->is_next_optional = false;
                            break;
                        }
                        return CND_ERR_CALLBACK;
                    }
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
                ctx->is_next_optional = false;
                break;
            }

            case OP_STR_PRE_U32: {
                uint16_t key = read_il_u16(ctx);
                const char* str = NULL;
                // printf("VM_DEBUG: Calling callback for STR_PRE_U32 (Key %d)\n", key);
                if (ctx->mode == CND_MODE_ENCODE) {
                    if (ctx->io_callback(ctx, key, opcode, &str) != CND_ERR_OK) {
                        if (ctx->is_next_optional) {
                            ctx->is_next_optional = false;
                            break;
                        }
                        return CND_ERR_CALLBACK;
                    }
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
                ctx->is_next_optional = false;
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
                if (count > 0) {
                    loop_push(ctx, ctx->ip, count);
                } else {
                     // Skip loop body if count is 0
                     int depth = 1;
                     while (ctx->ip < ctx->program->bytecode_len && depth > 0) {
                         uint8_t op = ctx->program->bytecode[ctx->ip];
                         if (op == OP_ARR_FIXED || op == OP_ARR_PRE_U8 || 
                             op == OP_ARR_PRE_U16 || op == OP_ARR_PRE_U32) depth++;
                         if (op == OP_ARR_END) depth--;
                         ctx->ip++; 
                     }
                }
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
                if (count > 0) {
                    loop_push(ctx, ctx->ip, count);
                } else {
                     int depth = 1;
                     while (ctx->ip < ctx->program->bytecode_len && depth > 0) {
                         uint8_t op = ctx->program->bytecode[ctx->ip];
                         if (op == OP_ARR_FIXED || op == OP_ARR_PRE_U8 || 
                             op == OP_ARR_PRE_U16 || op == OP_ARR_PRE_U32) depth++;
                         if (op == OP_ARR_END) depth--;
                         ctx->ip++; 
                     }
                }
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
                if (count > 0) {
                    loop_push(ctx, ctx->ip, count);
                } else {
                     int depth = 1;
                     while (ctx->ip < ctx->program->bytecode_len && depth > 0) {
                         uint8_t op = ctx->program->bytecode[ctx->ip];
                         if (op == OP_ARR_FIXED || op == OP_ARR_PRE_U8 || 
                             op == OP_ARR_PRE_U16 || op == OP_ARR_PRE_U32) depth++;
                         if (op == OP_ARR_END) depth--;
                         ctx->ip++; 
                     }
                }
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
                     while (ctx->ip < ctx->program->bytecode_len && depth > 0) {
                         uint8_t op = ctx->program->bytecode[ctx->ip];
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

