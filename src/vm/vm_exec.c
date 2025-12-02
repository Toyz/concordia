#include "vm_internal.h"
#include <string.h>
#include <stdio.h> // For debug prints

// --- Instruction Fetch Macros ---
// Can be overridden in cnd_execute for optimization
#define FETCH_IL_U8(ctx) read_il_u8(ctx)
#define FETCH_IL_U16(ctx) read_il_u16(ctx)

// --- Helpers / Macros to reduce repeated IO case code ---
// HANDLE_PRIMITIVE(size, ctype, READ_EXPR, WRITE_EXPR)
//   - size: number of bytes this IO consumes
//   - ctype: C type used for the value (e.g. uint8_t)
//   - READ_EXPR: expression to evaluate to read `ctype` from data buffer
//   - WRITE_EXPR: expression to evaluate to write `val` into buffer
#define HANDLE_PRIMITIVE(size, ctype, READ_EXPR, WRITE_EXPR) \
    { uint16_t key = FETCH_IL_U16(ctx); \
      if (ctx->cursor + (size) > ctx->data_len) { \
          if (ctx->is_next_optional) { \
              ctx->is_next_optional = false; \
              ctype val = 0; \
              SYNC_IP(); \
              if (ctx->io_callback(ctx, key, opcode, &val) != CND_ERR_OK) return CND_ERR_CALLBACK; \
              break; \
          } \
          return CND_ERR_OOB; \
      } \
      if (ctx->trans_type != CND_TRANS_NONE) { \
          if (ctx->trans_type == CND_TRANS_SCALE_F64) { \
              double eng_val = 0; \
              if (ctx->mode == CND_MODE_ENCODE) { \
                  SYNC_IP(); \
                  if (ctx->io_callback(ctx, key, OP_IO_F64, &eng_val) != CND_ERR_OK) return CND_ERR_CALLBACK; \
                  ctype val = (ctype)((eng_val - ctx->trans_f_offset) / ctx->trans_f_factor); \
                  WRITE_EXPR; \
              } else { \
                  ctype raw = (READ_EXPR); \
                  eng_val = (double)raw * ctx->trans_f_factor + ctx->trans_f_offset; \
                  SYNC_IP(); \
                  if (ctx->io_callback(ctx, key, OP_IO_F64, &eng_val) != CND_ERR_OK) return CND_ERR_CALLBACK; \
              } \
          } else if (ctx->trans_type == CND_TRANS_POLY) { \
              double eng_val = 0; \
              if (ctx->mode == CND_MODE_ENCODE) { \
                  SYNC_IP(); \
                  if (ctx->io_callback(ctx, key, OP_IO_F64, &eng_val) != CND_ERR_OK) return CND_ERR_CALLBACK; \
                  double x = 0; \
                  for(int iter=0; iter<20; iter++) { \
                      double y = 0; \
                      double dy = 0; \
                      if (ctx->trans_poly_count > 0) { \
                          uint64_t tmp; \
                          memcpy(&tmp, ctx->trans_poly_data + (ctx->trans_poly_count - 1) * 8, 8); \
                          memcpy(&y, &tmp, 8); \
                          for (int i = ctx->trans_poly_count - 2; i >= 0; i--) { \
                              double c; \
                              memcpy(&tmp, ctx->trans_poly_data + i * 8, 8); \
                              memcpy(&c, &tmp, 8); \
                              dy = dy * x + y; \
                              y = y * x + c; \
                          } \
                      } \
                      double diff = y - eng_val; \
                      if (diff > -0.001 && diff < 0.001) break; \
                      if (dy == 0) break; \
                      x = x - diff / dy; \
                  } \
                  ctype val = (ctype)x; \
                  WRITE_EXPR; \
              } else { \
                  ctype raw = (READ_EXPR); \
                  double x = (double)raw; \
                  double y = 0; \
                  if (ctx->trans_poly_count > 0) { \
                      uint64_t tmp; \
                      memcpy(&tmp, ctx->trans_poly_data + (ctx->trans_poly_count - 1) * 8, 8); \
                      memcpy(&y, &tmp, 8); \
                      for (int i = ctx->trans_poly_count - 2; i >= 0; i--) { \
                          double c; \
                          memcpy(&tmp, ctx->trans_poly_data + i * 8, 8); \
                          memcpy(&c, &tmp, 8); \
                          y = y * x + c; \
                      } \
                  } \
                  eng_val = y; \
                  SYNC_IP(); \
                  if (ctx->io_callback(ctx, key, OP_IO_F64, &eng_val) != CND_ERR_OK) return CND_ERR_CALLBACK; \
              } \
          } else if (ctx->trans_type == CND_TRANS_SPLINE) { \
              double eng_val = 0; \
              if (ctx->mode == CND_MODE_ENCODE) { \
                  SYNC_IP(); \
                  if (ctx->io_callback(ctx, key, OP_IO_F64, &eng_val) != CND_ERR_OK) return CND_ERR_CALLBACK; \
                  double x = 0; \
                  /* Reverse Spline: Find x given y (eng_val) */ \
                  if (ctx->trans_spline_count >= 2) { \
                      for (int i = 0; i < ctx->trans_spline_count - 1; i++) { \
                          double x0, y0, x1, y1; \
                          uint64_t tmp; \
                          memcpy(&tmp, ctx->trans_spline_data + (i * 2) * 8, 8); memcpy(&x0, &tmp, 8); \
                          memcpy(&tmp, ctx->trans_spline_data + (i * 2 + 1) * 8, 8); memcpy(&y0, &tmp, 8); \
                          memcpy(&tmp, ctx->trans_spline_data + ((i + 1) * 2) * 8, 8); memcpy(&x1, &tmp, 8); \
                          memcpy(&tmp, ctx->trans_spline_data + ((i + 1) * 2 + 1) * 8, 8); memcpy(&y1, &tmp, 8); \
                          if ((eng_val >= y0 && eng_val <= y1) || (eng_val <= y0 && eng_val >= y1)) { \
                              if (y1 == y0) x = x0; \
                              else x = x0 + (eng_val - y0) * (x1 - x0) / (y1 - y0); \
                              break; \
                          } \
                      } \
                  } \
                  ctype val = (ctype)x; \
                  WRITE_EXPR; \
              } else { \
                  ctype raw = (READ_EXPR); \
                  double x = (double)raw; \
                  double y = 0; \
                  /* Forward Spline: Find y given x (raw) */ \
                  if (ctx->trans_spline_count >= 2) { \
                      for (int i = 0; i < ctx->trans_spline_count - 1; i++) { \
                          double x0, y0, x1, y1; \
                          uint64_t tmp; \
                          memcpy(&tmp, ctx->trans_spline_data + (i * 2) * 8, 8); memcpy(&x0, &tmp, 8); \
                          memcpy(&tmp, ctx->trans_spline_data + (i * 2 + 1) * 8, 8); memcpy(&y0, &tmp, 8); \
                          memcpy(&tmp, ctx->trans_spline_data + ((i + 1) * 2) * 8, 8); memcpy(&x1, &tmp, 8); \
                          memcpy(&tmp, ctx->trans_spline_data + ((i + 1) * 2 + 1) * 8, 8); memcpy(&y1, &tmp, 8); \
                          if ((x >= x0 && x <= x1) || (i == ctx->trans_spline_count - 2)) { \
                              if (x1 == x0) y = y0; \
                              else y = y0 + (x - x0) * (y1 - y0) / (x1 - x0); \
                              break; \
                          } \
                      } \
                  } \
                  eng_val = y; \
                  SYNC_IP(); \
                  if (ctx->io_callback(ctx, key, OP_IO_F64, &eng_val) != CND_ERR_OK) return CND_ERR_CALLBACK; \
              } \
          } else { \
              int64_t eng_val = 0; \
              if (ctx->mode == CND_MODE_ENCODE) { \
                  SYNC_IP(); \
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
                  SYNC_IP(); \
                  if (ctx->io_callback(ctx, key, OP_IO_I64, &eng_val) != CND_ERR_OK) return CND_ERR_CALLBACK; \
              } \
          } \
          ctx->trans_type = CND_TRANS_NONE; \
      } else { \
          ctype val = 0; \
          if (ctx->mode == CND_MODE_ENCODE) { \
              SYNC_IP(); \
              if (ctx->io_callback(ctx, key, opcode, &val) != CND_ERR_OK) return CND_ERR_CALLBACK; \
              WRITE_EXPR; \
          } else { \
              val = (READ_EXPR); \
              SYNC_IP(); \
              if (ctx->io_callback(ctx, key, opcode, &val) != CND_ERR_OK) return CND_ERR_CALLBACK; \
          } \
      } \
      ctx->cursor += (size); \
      ctx->is_next_optional = false; \
      break; }

// Helper for float/double where we must memcopy to/from an integer representation
#define HANDLE_FLOAT(size, ctype, int_t, READ_INT_EXPR, WRITE_INT_EXPR) \
    { uint16_t key = FETCH_IL_U16(ctx); \
      if (ctx->cursor + (size) > ctx->data_len) { \
          if (ctx->is_next_optional) { \
              ctx->is_next_optional = false; \
              ctype val = 0; \
              SYNC_IP(); \
              if (ctx->io_callback(ctx, key, opcode, &val) != CND_ERR_OK) return CND_ERR_CALLBACK; \
              break; \
          } \
          return CND_ERR_OOB; \
      } \
      if (ctx->trans_type != CND_TRANS_NONE) { \
          if (ctx->trans_type == CND_TRANS_SCALE_F64) { \
              double eng_val = 0; \
              if (ctx->mode == CND_MODE_ENCODE) { \
                  SYNC_IP(); \
                  if (ctx->io_callback(ctx, key, OP_IO_F64, &eng_val) != CND_ERR_OK) return CND_ERR_CALLBACK; \
                  ctype val = (ctype)((eng_val - ctx->trans_f_offset) / ctx->trans_f_factor); \
                  int_t t; memcpy(&t, &val, sizeof(t)); WRITE_INT_EXPR; \
              } else { \
                  int_t t = (READ_INT_EXPR); ctype val; memcpy(&val, &t, sizeof(t)); \
                  eng_val = (double)val * ctx->trans_f_factor + ctx->trans_f_offset; \
                  SYNC_IP(); \
                  if (ctx->io_callback(ctx, key, OP_IO_F64, &eng_val) != CND_ERR_OK) return CND_ERR_CALLBACK; \
              } \
          } else { \
              int64_t eng_val = 0; \
              if (ctx->mode == CND_MODE_ENCODE) { \
                  SYNC_IP(); \
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
                  SYNC_IP(); \
                  if (ctx->io_callback(ctx, key, OP_IO_I64, &eng_val) != CND_ERR_OK) return CND_ERR_CALLBACK; \
              } \
          } \
          ctx->trans_type = CND_TRANS_NONE; \
      } else { \
          ctype val = 0; \
          if (ctx->mode == CND_MODE_ENCODE) { \
              SYNC_IP(); \
              if (ctx->io_callback(ctx, key, opcode, &val) != CND_ERR_OK) return CND_ERR_CALLBACK; \
              int_t t; memcpy(&t, &val, sizeof(t)); WRITE_INT_EXPR; \
          } else { \
              int_t t = (READ_INT_EXPR); memcpy(&val, &t, sizeof(t)); \
              SYNC_IP(); \
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

static void skip_loop_body(cnd_vm_ctx* ctx) {
    int depth = 1;
    while (ctx->ip < ctx->program->bytecode_len && depth > 0) {
        uint8_t op = ctx->program->bytecode[ctx->ip];
        if (op == OP_ARR_FIXED || op == OP_ARR_PRE_U8 || 
            op == OP_ARR_PRE_U16 || op == OP_ARR_PRE_U32) depth++;
        if (op == OP_ARR_END) depth--;
        ctx->ip++; 
    }
}

static bool loop_push(cnd_vm_ctx* ctx, size_t start_ip, uint32_t count) {
    if (ctx->loop_depth >= CND_MAX_LOOP_DEPTH) return false;
    ctx->loop_stack[ctx->loop_depth].start_ip = start_ip;
    ctx->loop_stack[ctx->loop_depth].remaining = count;
    ctx->loop_depth++;
    return true;
}

static void loop_pop(cnd_vm_ctx* ctx) {
    if (ctx->loop_depth > 0) ctx->loop_depth--;
}

// --- Array/String Helpers ---

#define HANDLE_ARRAY_PRE(size, ctype, READ_EXPR, WRITE_EXPR) \
    { \
        uint16_t key = FETCH_IL_U16(ctx); \
        ctype count = 0; \
        if (ctx->mode == CND_MODE_ENCODE) { \
            SYNC_IP(); \
            if (ctx->io_callback(ctx, key, opcode, &count) != CND_ERR_OK) return CND_ERR_CALLBACK; \
            if (ctx->cursor + (size) > ctx->data_len) return CND_ERR_OOB; \
            WRITE_EXPR; \
            ctx->cursor += (size); \
        } else { \
            if (ctx->cursor + (size) > ctx->data_len) return CND_ERR_OOB; \
            count = (READ_EXPR); \
            ctx->cursor += (size); \
            SYNC_IP(); \
            if (ctx->io_callback(ctx, key, opcode, &count) != CND_ERR_OK) return CND_ERR_CALLBACK; \
        } \
        if (count > 0) { \
            SYNC_IP(); \
            if (!loop_push(ctx, ctx->ip, (uint32_t)count)) return CND_ERR_OOB; \
        } else { \
             SYNC_IP(); \
             skip_loop_body(ctx); \
             RELOAD_PC(); \
        } \
        break; \
    }

#define HANDLE_STRING_PRE(size, ctype, READ_EXPR, WRITE_EXPR) \
    { \
        uint16_t key = FETCH_IL_U16(ctx); \
        const char* str = NULL; \
        if (ctx->mode == CND_MODE_ENCODE) { \
            SYNC_IP(); \
            if (ctx->io_callback(ctx, key, opcode, &str) != CND_ERR_OK) { \
                if (ctx->is_next_optional) { \
                    ctx->is_next_optional = false; \
                    break; \
                } \
                return CND_ERR_CALLBACK; \
            } \
            size_t len = str ? strlen(str) : 0; \
            ctype max_val = (ctype)-1; \
            if (len > (size_t)max_val) len = (size_t)max_val; \
            if (ctx->cursor + (size) + len > ctx->data_len) return CND_ERR_OOB; \
            ctype len_val = (ctype)len; \
            WRITE_EXPR; \
            memcpy(ctx->data_buffer + ctx->cursor + (size), str, len); \
            ctx->cursor += (size) + len; \
        } else { \
            if (ctx->cursor + (size) > ctx->data_len) return CND_ERR_OOB; \
            ctype len_val = (READ_EXPR); \
            if (ctx->cursor + (size) + len_val > ctx->data_len) return CND_ERR_OOB; \
            const char* ptr = (const char*)(ctx->data_buffer + ctx->cursor + (size)); \
            SYNC_IP(); \
            if (ctx->io_callback(ctx, key, opcode, (void*)ptr) != CND_ERR_OK) return CND_ERR_CALLBACK; \
            ctx->cursor += (size) + len_val; \
        } \
        ctx->is_next_optional = false; \
        break; \
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

static const uint32_t crc32_table[256] = {
    0x00000000, 0x77073096, 0xee0e612c, 0x990951ba, 0x076dc419, 0x706af48f, 0xe963a535, 0x9e6495a3,
    0x0edb8832, 0x79dcb8a4, 0xe0d5e91e, 0x97d2d988, 0x09b64c2b, 0x7eb17cbd, 0xe7b82d07, 0x90bf1d91,
    0x1db71064, 0x6ab020f2, 0xf3b97148, 0x84be41de, 0x1adad47d, 0x6ddde4eb, 0xf4d4b551, 0x83d385c7,
    0x136c9856, 0x646ba8c0, 0xfd62f97a, 0x8a65c9ec, 0x14015c4f, 0x63066cd9, 0xfa0f3d63, 0x8d080df5,
    0x3b6e20c8, 0x4c69105e, 0xd56041e4, 0xa2677172, 0x3c03e4d1, 0x4b04d447, 0xd20d85fd, 0xa50ab56b,
    0x35b5a8fa, 0x42b2986c, 0xdbbbc9d6, 0xacbcf940, 0x32d86ce3, 0x45df5c75, 0xdcd60dcf, 0xabd13d59,
    0x26d930ac, 0x51de003a, 0xc8d75180, 0xbfd06116, 0x21b4f4b5, 0x56b3c423, 0xcfba9599, 0xb8bda50f,
    0x2802b89e, 0x5f058808, 0xc60cd9b2, 0xb10be924, 0x2f6f7c87, 0x58684c11, 0xc1611dab, 0xb6662d3d,
    0x76dc4190, 0x01db7106, 0x98d220bc, 0xefd5102a, 0x71b18589, 0x06b6b51f, 0x9fbfe4a5, 0xe8b8d433,
    0x7807c9a2, 0x0f00f934, 0x9609a88e, 0xe10e9818, 0x7f6a0dbb, 0x086d3d2d, 0x91646c97, 0xe6635c01,
    0x6b6b51f4, 0x1c6c6162, 0x856530d8, 0xf262004e, 0x6c0695ed, 0x1b01a57b, 0x8208f4c1, 0xf50fc457,
    0x65b0d9c6, 0x12b7e950, 0x8bbeb8ea, 0xfcb9887c, 0x62dd1ddf, 0x15da2d49, 0x8cd37cf3, 0xfbd44c65,
    0x4db26158, 0x3ab551ce, 0xa3bc0074, 0xd4bb30e2, 0x4adfa541, 0x3dd895d7, 0xa4d1c46d, 0xd3d6f4fb,
    0x4369e96a, 0x346ed9fc, 0xad678846, 0xda60b8d0, 0x44042d73, 0x33031de5, 0xaa0a4c5f, 0xdd0d7cc9,
    0x5005713c, 0x270241aa, 0xbe0b1010, 0xc90c2086, 0x5768b525, 0x206f85b3, 0xb966d409, 0xce61e49f,
    0x5edef90e, 0x29d9c998, 0xb0d09822, 0xc7d7a8b4, 0x59b33d17, 0x2eb40d81, 0xb7bd5c3b, 0xc0ba6cad,
    0xedb88320, 0x9abfb3b6, 0x03b6e20c, 0x74b1d29a, 0xead54739, 0x9dd277af, 0x04db2615, 0x73dc1683,
    0xe3630b12, 0x94643b84, 0x0d6d6a3e, 0x7a6a5aa8, 0xe40ecf0b, 0x9309ff9d, 0x0a00ae27, 0x7d079eb1,
    0xf00f9344, 0x8708a3d2, 0x1e01f268, 0x6906c2fe, 0xf762575d, 0x806567cb, 0x196c3671, 0x6e6b06e7,
    0xfed41b76, 0x89d32be0, 0x10da7a5a, 0x67dd4acc, 0xf9b9df6f, 0x8ebeeff9, 0x17b7be43, 0x60b08ed5,
    0xd6d6a3e8, 0xa1d1937e, 0x38d8c2c4, 0x4fdff252, 0xd1bb67f1, 0xa6bc5767, 0x3fb506dd, 0x48b2364b,
    0xd80d2bda, 0xaf0a1b4c, 0x36034af6, 0x41047a60, 0xdf60efc3, 0xa867df55, 0x316e8eef, 0x4669be79,
    0xcb61b38c, 0xbc66831a, 0x256fd2a0, 0x5268e236, 0xcc0c7795, 0xbb0b4703, 0x220216b9, 0x5505262f,
    0xc5ba3bbe, 0xb2bd0b28, 0x2bb45a92, 0x5cb36a04, 0xc2d7ffa7, 0xb5d0cf31, 0x2cd99e8b, 0x5bdeae1d,
    0x9b64c2b0, 0xec63f226, 0x756aa39c, 0x026d930a, 0x9c0906a9, 0xeb0e363f, 0x72076785, 0x05005713,
    0x95bf4a82, 0xe2b87a14, 0x7bb12bae, 0x0cb61b38, 0x92d28e9b, 0xe5d5be0d, 0x7cdcefb7, 0x0bdbdf21,
    0x86d3d2d4, 0xf1d4e242, 0x68ddb3f8, 0x1fda836e, 0x81be16cd, 0xf6b9265b, 0x6fb077e1, 0x18b74777,
    0x88085ae6, 0xff0f6a70, 0x66063bca, 0x11010b5c, 0x8f659eff, 0xf862ae69, 0x616bffd3, 0x166ccf45,
    0xa00ae278, 0xd70dd2ee, 0x4e048354, 0x3903b3c2, 0xa7672661, 0xd06016f7, 0x4969474d, 0x3e6e77db,
    0xaed16a4a, 0xd9d65adc, 0x40df0b66, 0x37d83bf0, 0xa9bcae53, 0xdebb9ec5, 0x47b2cf7f, 0x30b5ffe9,
    0xbdbdf21c, 0xcabac28a, 0x53b39330, 0x24b4a3a6, 0xbad03605, 0xcdd70693, 0x54de5729, 0x23d967bf,
    0xb3667a2e, 0xc4614ab8, 0x5d681b02, 0x2a6f2b94, 0xb40bbe37, 0xc30c8ea1, 0x5a05df1b, 0x2d02ef8d
};

static uint32_t calc_crc(const uint8_t* data, size_t len, uint32_t poly, uint32_t init, uint32_t xorout, uint8_t flags, int width) {
    // Fast path for Standard CRC32 (Poly 0x04C11DB7, RefIn=1, RefOut=1)
    if (width == 32 && poly == 0x04C11DB7 && (flags & 1) && (flags & 2)) {
        uint32_t crc = init;
        for (size_t i = 0; i < len; i++) {
            crc = (crc >> 8) ^ crc32_table[(crc ^ data[i]) & 0xFF];
        }
        return crc ^ xorout;
    }

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

static const bool ALIGN_TABLE[256] = {
    // 0x00 - 0x0F (Meta & State) - No alignment
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    // 0x10 - 0x1F (Primitives) - Align
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    // 0x20 - 0x2F (Bitfields) - No alignment
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    // 0x30 - 0x3F (Arrays & Strings) - Align
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    // 0x40 - 0x4F (Validation)
    1, // 0x40 CONST_CHECK
    1, // 0x41 CONST_WRITE
    0, // 0x42 RANGE_CHECK
    0, // 0x43 SCALE_LIN
    1, // 0x44 CRC_16
    0, // 0x45 TRANS_ADD
    0, // 0x46 TRANS_SUB
    0, // 0x47 TRANS_MUL
    0, // 0x48 TRANS_DIV
    1, // 0x49 CRC_32
    0, // 0x4A MARK_OPTIONAL
    1, // 0x4B ENUM_CHECK
    1, 1, 1, 1, // 0x4C - 0x4F
    // 0x50 - 0xFF (Control Flow & Others) - No alignment
    0 // ... rest 0
};

static inline bool should_align(uint8_t opcode) {
    return ALIGN_TABLE[opcode];
}

// --- Public API ---

void cnd_program_load(cnd_program* program, const uint8_t* bytecode, size_t len) {
    if (!program) return;
    program->bytecode = bytecode;
    program->bytecode_len = len;
    program->string_table = NULL;
    program->string_count = 0;
}

cnd_error_t cnd_program_load_il(cnd_program* program, const uint8_t* image, size_t len) {
    if (!program || !image) return CND_ERR_INVALID_OP;
    
    // Header Check: "CNDIL" (5 bytes) + Ver (1 byte) + StrCount (2) + StrOff (4) + BCOff (4) = 16 bytes
    if (len < 16) return CND_ERR_OOB;
    if (memcmp(image, "CNDIL", 5) != 0) return CND_ERR_INVALID_OP;
    if (image[5] != 1) return CND_ERR_INVALID_OP; // Version check

    uint16_t str_count = image[6] | (image[7] << 8);
    uint32_t str_offset = image[8] | (image[9] << 8) | (image[10] << 16) | (image[11] << 24);
    uint32_t bc_offset = image[12] | (image[13] << 8) | (image[14] << 16) | (image[15] << 24);

    if (str_offset > len || bc_offset > len) return CND_ERR_OOB;
    
    program->string_table = (const char*)(image + str_offset);
    program->string_count = str_count;
    program->bytecode = image + bc_offset;
    program->bytecode_len = len - bc_offset;
    
    return CND_ERR_OK;
}

const char* cnd_get_key_name(const cnd_program* program, uint16_t key_id) {
    if (!program || !program->string_table || key_id >= program->string_count) return NULL;
    
    const char* ptr = program->string_table;
    for (uint16_t i = 0; i < key_id; i++) {
        while (*ptr) ptr++; // Skip current string
        ptr++; // Skip null terminator
    }
    return ptr;
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
    ctx->expr_sp = 0;
    
    ctx->trans_type = CND_TRANS_NONE;
    ctx->trans_f_factor = 1.0;
    ctx->trans_f_offset = 0.0;
    ctx->trans_i_val = 0;
    
    ctx->is_next_optional = false;
}

// Helper for stack operations
static inline cnd_error_t stack_push(cnd_vm_ctx* ctx, uint64_t val) {
    if (ctx->expr_sp >= CND_MAX_EXPR_STACK) return CND_ERR_STACK_OVERFLOW;
    ctx->expr_stack[ctx->expr_sp++] = val;
    return CND_ERR_OK;
}

static inline cnd_error_t stack_pop(cnd_vm_ctx* ctx, uint64_t* val) {
    if (ctx->expr_sp == 0) return CND_ERR_STACK_UNDERFLOW;
    *val = ctx->expr_stack[--ctx->expr_sp];
    return CND_ERR_OK;
}

// Helper for binary operations
#define BINARY_OP(OP) \
    uint64_t b; if (stack_pop(ctx, &b) != CND_ERR_OK) return CND_ERR_STACK_UNDERFLOW; \
    uint64_t a; if (stack_pop(ctx, &a) != CND_ERR_OK) return CND_ERR_STACK_UNDERFLOW; \
    if (stack_push(ctx, a OP b) != CND_ERR_OK) return CND_ERR_STACK_OVERFLOW;

// Helper for unary operations
#define UNARY_OP(OP) \
    uint64_t a; if (stack_pop(ctx, &a) != CND_ERR_OK) return CND_ERR_STACK_UNDERFLOW; \
    if (stack_push(ctx, OP a) != CND_ERR_OK) return CND_ERR_STACK_OVERFLOW;

cnd_error_t cnd_execute(cnd_vm_ctx* ctx) {
    if (!ctx || !ctx->program || !ctx->program->bytecode || !ctx->data_buffer) return CND_ERR_OOB;

    const uint8_t* pc = ctx->program->bytecode + ctx->ip;
    const uint8_t* end = ctx->program->bytecode + ctx->program->bytecode_len;

    // Override fetch macros to use local pc
    #undef FETCH_IL_U8
    #undef FETCH_IL_U16
    #define FETCH_IL_U8(c) ((pc < end) ? *pc++ : 0)
    #define FETCH_IL_U16(c) ((pc + 2 <= end) ? (pc += 2, (uint16_t)(pc[-2] | (pc[-1] << 8))) : 0)
    #define FETCH_IL_U32(c) ((pc + 4 <= end) ? (pc += 4, (uint32_t)(pc[-4] | (pc[-3] << 8) | (pc[-2] << 16) | (pc[-1] << 24))) : 0)
    #define FETCH_IL_U64(c) ((pc + 8 <= end) ? (pc += 8, ((uint64_t)pc[-8] | ((uint64_t)pc[-7] << 8) | ((uint64_t)pc[-6] << 16) | ((uint64_t)pc[-5] << 24) | ((uint64_t)pc[-4] << 32) | ((uint64_t)pc[-3] << 40) | ((uint64_t)pc[-2] << 48) | ((uint64_t)pc[-1] << 56))) : 0)
    
    #define SYNC_IP() (ctx->ip = (size_t)(pc - ctx->program->bytecode))
    #define RELOAD_PC() (pc = ctx->program->bytecode + ctx->ip)

    while (pc < end) {
        uint8_t opcode = *pc++;
        // printf("Opcode: %02X at IP %zu\n", opcode, (size_t)(pc - ctx->program->bytecode - 1));

        // Check alignment
        if (ALIGN_TABLE[opcode]) {
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
                uint16_t key = FETCH_IL_U16(ctx);
                // printf("VM_DEBUG: Calling callback for ENTER_STRUCT (Key %d)\n", key);
                SYNC_IP();
                if (ctx->io_callback(ctx, key, opcode, NULL) != CND_ERR_OK) return CND_ERR_CALLBACK;
                break; // VM continues with next instruction, which is the first field of the struct
            }
            
            case OP_EXIT_STRUCT: {
                // printf("VM_DEBUG: Calling callback for EXIT_STRUCT\n");
                SYNC_IP();
                if (ctx->io_callback(ctx, 0, opcode, NULL) != CND_ERR_OK) return CND_ERR_CALLBACK;
                break; // VM continues with next instruction after the struct
            }

            case OP_META_VERSION: {
                FETCH_IL_U8(ctx); // Skip version
                break;
            }

            case OP_META_NAME: {
                FETCH_IL_U16(ctx); // Skip name key
                break;
            }
            
            case OP_CONST_WRITE: {
                uint8_t type = FETCH_IL_U8(ctx);
                uint64_t val = 0;
                int size = 0;
                if (type == OP_IO_U8) { val = FETCH_IL_U8(ctx); size=1; }
                else if (type == OP_IO_U16) { val = FETCH_IL_U16(ctx); size=2; }
                else if (type == OP_IO_U32) { val = FETCH_IL_U32(ctx); size=4; }
                else if (type == OP_IO_U64) { val = FETCH_IL_U64(ctx); size=8; }
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
                uint16_t key = FETCH_IL_U16(ctx);
                uint8_t type = FETCH_IL_U8(ctx);
                uint64_t expected = 0;
                int size = 0;
                if (type == OP_IO_U8 || type == OP_IO_I8) { expected = FETCH_IL_U8(ctx); size=1; }
                else if (type == OP_IO_U16 || type == OP_IO_I16) { expected = FETCH_IL_U16(ctx); size=2; }
                else if (type == OP_IO_U32 || type == OP_IO_I32) { expected = FETCH_IL_U32(ctx); size=4; }
                else if (type == OP_IO_U64 || type == OP_IO_I64) { expected = FETCH_IL_U64(ctx); size=8; }
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

                    // Notify host (Read-Only)
                    SYNC_IP();
                    if (size == 1) { uint8_t v = (uint8_t)actual; if (ctx->io_callback(ctx, key, type, &v) != CND_ERR_OK) return CND_ERR_CALLBACK; }
                    else if (size == 2) { uint16_t v = (uint16_t)actual; if (ctx->io_callback(ctx, key, type, &v) != CND_ERR_OK) return CND_ERR_CALLBACK; }
                    else if (size == 4) { uint32_t v = (uint32_t)actual; if (ctx->io_callback(ctx, key, type, &v) != CND_ERR_OK) return CND_ERR_CALLBACK; }
                    else if (size == 8) { uint64_t v = actual; if (ctx->io_callback(ctx, key, type, &v) != CND_ERR_OK) return CND_ERR_CALLBACK; }
                }
                
                ctx->cursor += size;
                break;
            }

            case OP_ENUM_CHECK: {
                uint8_t type = FETCH_IL_U8(ctx);
                uint16_t count = FETCH_IL_U16(ctx);
                bool found = false;
                
                #define CHECK_ENUM(size, ctype, READ_EXPR) \
                    ctype actual = (READ_EXPR); \
                    for (uint16_t i = 0; i < count; i++) { \
                        ctype val = 0; \
                        if (size == 1) val = (ctype)FETCH_IL_U8(ctx); \
                        else if (size == 2) val = (ctype)FETCH_IL_U16(ctx); \
                        else if (size == 4) val = (ctype)FETCH_IL_U32(ctx); \
                        else if (size == 8) val = (ctype)FETCH_IL_U64(ctx); \
                        if (actual == val) { \
                            found = true; \
                            pc += (count - 1 - i) * size; \
                            break; \
                        } \
                    }

                switch (type) {
                    case OP_IO_U8:  { CHECK_ENUM(1, uint8_t, read_u8(ctx->data_buffer + ctx->cursor - 1)); break; }
                    case OP_IO_I8:  { CHECK_ENUM(1, int8_t,  (int8_t)read_u8(ctx->data_buffer + ctx->cursor - 1)); break; }
                    case OP_IO_U16: { CHECK_ENUM(2, uint16_t, read_u16(ctx->data_buffer + ctx->cursor - 2, ctx->endianness)); break; }
                    case OP_IO_I16: { CHECK_ENUM(2, int16_t,  (int16_t)read_u16(ctx->data_buffer + ctx->cursor - 2, ctx->endianness)); break; }
                    case OP_IO_U32: { CHECK_ENUM(4, uint32_t, read_u32(ctx->data_buffer + ctx->cursor - 4, ctx->endianness)); break; }
                    case OP_IO_I32: { CHECK_ENUM(4, int32_t,  (int32_t)read_u32(ctx->data_buffer + ctx->cursor - 4, ctx->endianness)); break; }
                    case OP_IO_U64: { CHECK_ENUM(8, uint64_t, read_u64(ctx->data_buffer + ctx->cursor - 8, ctx->endianness)); break; }
                    case OP_IO_I64: { CHECK_ENUM(8, int64_t,  (int64_t)read_u64(ctx->data_buffer + ctx->cursor - 8, ctx->endianness)); break; }
                    default: return CND_ERR_INVALID_OP;
                }
                #undef CHECK_ENUM
                
                if (!found) return CND_ERR_VALIDATION;
                break;
            }

            case OP_RANGE_CHECK: {
                uint8_t type = FETCH_IL_U8(ctx);
                switch (type) {
                    case OP_IO_U8:  CHECK_RANGE(1, uint8_t, FETCH_IL_U8(ctx), read_u8(ctx->data_buffer + ctx->cursor - 1)); break;
                    case OP_IO_I8:  CHECK_RANGE(1, int8_t,  FETCH_IL_U8(ctx), read_u8(ctx->data_buffer + ctx->cursor - 1)); break;
                    case OP_IO_U16: CHECK_RANGE(2, uint16_t, FETCH_IL_U16(ctx), read_u16(ctx->data_buffer + ctx->cursor - 2, ctx->endianness)); break;
                    case OP_IO_I16: CHECK_RANGE(2, int16_t,  FETCH_IL_U16(ctx), read_u16(ctx->data_buffer + ctx->cursor - 2, ctx->endianness)); break;
                    case OP_IO_U32: CHECK_RANGE(4, uint32_t, FETCH_IL_U32(ctx), read_u32(ctx->data_buffer + ctx->cursor - 4, ctx->endianness)); break;
                    case OP_IO_I32: CHECK_RANGE(4, int32_t,  FETCH_IL_U32(ctx), read_u32(ctx->data_buffer + ctx->cursor - 4, ctx->endianness)); break;
                    case OP_IO_U64: CHECK_RANGE(8, uint64_t, FETCH_IL_U64(ctx), read_u64(ctx->data_buffer + ctx->cursor - 8, ctx->endianness)); break;
                    case OP_IO_I64: CHECK_RANGE(8, int64_t,  FETCH_IL_U64(ctx), read_u64(ctx->data_buffer + ctx->cursor - 8, ctx->endianness)); break;
                    
                    case OP_IO_F32: CHECK_RANGE_FLOAT(4, float, uint32_t, FETCH_IL_U32(ctx), read_u32(ctx->data_buffer + ctx->cursor - 4, ctx->endianness)); break;
                    case OP_IO_F64: CHECK_RANGE_FLOAT(8, double, uint64_t, FETCH_IL_U64(ctx), read_u64(ctx->data_buffer + ctx->cursor - 8, ctx->endianness)); break;
                    
                    default: return CND_ERR_INVALID_OP;
                }
                break;
            }

            case OP_CRC_16: {
                uint16_t poly = FETCH_IL_U16(ctx);
                uint16_t init = FETCH_IL_U16(ctx);
                uint16_t xorout = FETCH_IL_U16(ctx);
                uint8_t flags = FETCH_IL_U8(ctx);
                
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
                uint32_t poly = FETCH_IL_U32(ctx);
                uint32_t init = FETCH_IL_U32(ctx);
                uint32_t xorout = FETCH_IL_U32(ctx);
                uint8_t flags = FETCH_IL_U8(ctx);
                
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
                uint64_t i_fac = FETCH_IL_U64(ctx);
                uint64_t i_off = FETCH_IL_U64(ctx);
                double fac, off;
                memcpy(&fac, &i_fac, 8);
                memcpy(&off, &i_off, 8);
                ctx->trans_type = CND_TRANS_SCALE_F64;
                ctx->trans_f_factor = fac;
                ctx->trans_f_offset = off;
                break;
            }

            case OP_TRANS_POLY: {
                uint8_t count = FETCH_IL_U8(ctx);
                ctx->trans_type = CND_TRANS_POLY;
                ctx->trans_poly_count = count;
                ctx->trans_poly_data = pc;
                pc += (count * 8);
                if (pc > end) return CND_ERR_OOB;
                break;
            }

            case OP_TRANS_SPLINE: {
                uint8_t count = FETCH_IL_U8(ctx);
                ctx->trans_type = CND_TRANS_SPLINE;
                ctx->trans_spline_count = count;
                ctx->trans_spline_data = pc;
                pc += (count * 2 * 8); // 2 doubles per point
                if (pc > end) return CND_ERR_OOB;
                break;
            }

            case OP_MARK_OPTIONAL:
                ctx->is_next_optional = true;
                break;
            
            case OP_TRANS_ADD: ctx->trans_type = CND_TRANS_ADD_I64; ctx->trans_i_val = (int64_t)FETCH_IL_U64(ctx); break;
            case OP_TRANS_SUB: ctx->trans_type = CND_TRANS_SUB_I64; ctx->trans_i_val = (int64_t)FETCH_IL_U64(ctx); break;
            case OP_TRANS_MUL: ctx->trans_type = CND_TRANS_MUL_I64; ctx->trans_i_val = (int64_t)FETCH_IL_U64(ctx); break;
            case OP_TRANS_DIV: ctx->trans_type = CND_TRANS_DIV_I64; ctx->trans_i_val = (int64_t)FETCH_IL_U64(ctx); break;

            // ... Category B (Primitives) ...
            case OP_IO_U8: HANDLE_PRIMITIVE(1, uint8_t, read_u8(ctx->data_buffer + ctx->cursor), write_u8(ctx->data_buffer + ctx->cursor, val));
            case OP_IO_U16: HANDLE_PRIMITIVE(2, uint16_t, read_u16(ctx->data_buffer + ctx->cursor, ctx->endianness), write_u16(ctx->data_buffer + ctx->cursor, val, ctx->endianness));
            case OP_IO_U32: HANDLE_PRIMITIVE(4, uint32_t, read_u32(ctx->data_buffer + ctx->cursor, ctx->endianness), write_u32(ctx->data_buffer + ctx->cursor, val, ctx->endianness));
            case OP_IO_U64: HANDLE_PRIMITIVE(8, uint64_t, read_u64(ctx->data_buffer + ctx->cursor, ctx->endianness), write_u64(ctx->data_buffer + ctx->cursor, val, ctx->endianness));
            
            case OP_IO_I8: HANDLE_PRIMITIVE(1, int8_t, (int8_t)read_u8(ctx->data_buffer + ctx->cursor), write_u8(ctx->data_buffer + ctx->cursor, (uint8_t)val));
            case OP_IO_I16: HANDLE_PRIMITIVE(2, int16_t, (int16_t)read_u16(ctx->data_buffer + ctx->cursor, ctx->endianness), write_u16(ctx->data_buffer + ctx->cursor, (uint16_t)val, ctx->endianness));
            case OP_IO_I32: HANDLE_PRIMITIVE(4, int32_t, (int32_t)read_u32(ctx->data_buffer + ctx->cursor, ctx->endianness), write_u32(ctx->data_buffer + ctx->cursor, (uint32_t)val, ctx->endianness));
            case OP_IO_I64: HANDLE_PRIMITIVE(8, int64_t, (int64_t)read_u64(ctx->data_buffer + ctx->cursor, ctx->endianness), write_u64(ctx->data_buffer + ctx->cursor, (uint64_t)val, ctx->endianness));

            case OP_IO_BOOL: {
                uint16_t key = FETCH_IL_U16(ctx);
                if (ctx->cursor + 1 > ctx->data_len) {
                    if (ctx->is_next_optional) {
                        ctx->is_next_optional = false;
                        uint8_t val = 0;
                        SYNC_IP();
                        if (ctx->io_callback(ctx, key, opcode, &val) != CND_ERR_OK) return CND_ERR_CALLBACK;
                        break;
                    }
                    return CND_ERR_OOB;
                }
                uint8_t val = 0;
                if (ctx->mode == CND_MODE_ENCODE) {
                    SYNC_IP();
                    if (ctx->io_callback(ctx, key, opcode, &val) != CND_ERR_OK) return CND_ERR_CALLBACK;
                    if (val > 1) return CND_ERR_VALIDATION;
                    write_u8(ctx->data_buffer + ctx->cursor, val);
                } else {
                    val = read_u8(ctx->data_buffer + ctx->cursor);
                    if (val > 1) return CND_ERR_VALIDATION;
                    SYNC_IP();
                    if (ctx->io_callback(ctx, key, opcode, &val) != CND_ERR_OK) return CND_ERR_CALLBACK;
                }
                ctx->cursor += 1;
                ctx->is_next_optional = false;
                break;
            }

            case OP_IO_F32: HANDLE_FLOAT(4, float, uint32_t, read_u32(ctx->data_buffer + ctx->cursor, ctx->endianness), write_u32(ctx->data_buffer + ctx->cursor, t, ctx->endianness));
            case OP_IO_F64: HANDLE_FLOAT(8, double, uint64_t, read_u64(ctx->data_buffer + ctx->cursor, ctx->endianness), write_u64(ctx->data_buffer + ctx->cursor, t, ctx->endianness));

            // ... Category C (Bitfields) ...
            case OP_IO_BIT_U: { uint16_t k=FETCH_IL_U16(ctx); uint8_t b=FETCH_IL_U8(ctx); uint64_t v=0; if(ctx->mode==CND_MODE_ENCODE){ SYNC_IP(); if(ctx->io_callback(ctx,k,opcode,&v)) return CND_ERR_CALLBACK; write_bits(ctx,v,b); } else { v=read_bits(ctx,b); SYNC_IP(); if(ctx->io_callback(ctx,k,opcode,&v)) return CND_ERR_CALLBACK; } break; } 
            case OP_IO_BIT_I: { 
                uint16_t k=FETCH_IL_U16(ctx); 
                uint8_t b=FETCH_IL_U8(ctx); 
                int64_t v=0; 
                if(ctx->mode==CND_MODE_ENCODE){ 
                    SYNC_IP();
                    if(ctx->io_callback(ctx,k,opcode,&v)) return CND_ERR_CALLBACK; 
                    write_bits(ctx,(uint64_t)v,b); 
                } else { 
                    uint64_t raw = read_bits(ctx,b); 
                    v = sign_extend(raw, b);
                    SYNC_IP();
                    if(ctx->io_callback(ctx,k,opcode,&v)) return CND_ERR_CALLBACK; 
                } 
                break; 
            }
            case OP_IO_BIT_BOOL: {
                uint16_t k = FETCH_IL_U16(ctx);
                uint8_t v = 0;
                if (ctx->mode == CND_MODE_ENCODE) {
                    SYNC_IP();
                    if (ctx->io_callback(ctx, k, opcode, &v) != CND_ERR_OK) return CND_ERR_CALLBACK;
                    if (v > 1) return CND_ERR_VALIDATION;
                    write_bits(ctx, (uint64_t)v, 1);
                } else {
                    uint64_t raw = read_bits(ctx, 1);
                    v = (uint8_t)raw;
                    SYNC_IP();
                    if (ctx->io_callback(ctx, k, opcode, &v) != CND_ERR_OK) return CND_ERR_CALLBACK;
                }
                break;
            }
            case OP_ALIGN_PAD: { uint8_t b=FETCH_IL_U8(ctx); for(int i=0;i<b;i++) { ctx->bit_offset++; if(ctx->bit_offset>=8){ctx->bit_offset=0; ctx->cursor++;} } break; } 
            case OP_ALIGN_FILL: { 
                uint8_t fill_bit = FETCH_IL_U8(ctx);
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

            // ... Category D: Arrays & Strings ...
            
            case OP_STR_NULL: {
                uint16_t key = FETCH_IL_U16(ctx);
                uint16_t max_len = FETCH_IL_U16(ctx);
                const char* str = NULL;
                // printf("VM_DEBUG: Calling callback for STR_NULL (Key %d)\n", key);
                if (ctx->mode == CND_MODE_ENCODE) {
                    SYNC_IP();
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
                    SYNC_IP();
                    if (ctx->io_callback(ctx, key, opcode, (void*)ptr) != CND_ERR_OK) return CND_ERR_CALLBACK;
                    
                    ctx->cursor++; // Skip null
                }
                ctx->is_next_optional = false;
                break;
            }

            case OP_STR_PRE_U8:  HANDLE_STRING_PRE(1, uint8_t,  read_u8(ctx->data_buffer + ctx->cursor), write_u8(ctx->data_buffer + ctx->cursor, len_val));
            case OP_STR_PRE_U16: HANDLE_STRING_PRE(2, uint16_t, read_u16(ctx->data_buffer + ctx->cursor, ctx->endianness), write_u16(ctx->data_buffer + ctx->cursor, len_val, ctx->endianness));
            case OP_STR_PRE_U32: HANDLE_STRING_PRE(4, uint32_t, read_u32(ctx->data_buffer + ctx->cursor, ctx->endianness), write_u32(ctx->data_buffer + ctx->cursor, len_val, ctx->endianness));

            case OP_ARR_PRE_U8:  HANDLE_ARRAY_PRE(1, uint8_t,  read_u8(ctx->data_buffer + ctx->cursor), write_u8(ctx->data_buffer + ctx->cursor, count));
            case OP_ARR_PRE_U16: HANDLE_ARRAY_PRE(2, uint16_t, read_u16(ctx->data_buffer + ctx->cursor, ctx->endianness), write_u16(ctx->data_buffer + ctx->cursor, count, ctx->endianness));
            case OP_ARR_PRE_U32: HANDLE_ARRAY_PRE(4, uint32_t, read_u32(ctx->data_buffer + ctx->cursor, ctx->endianness), write_u32(ctx->data_buffer + ctx->cursor, count, ctx->endianness));

            case OP_ARR_FIXED: {
                uint16_t key = FETCH_IL_U16(ctx);
                uint32_t count = FETCH_IL_U32(ctx);
                // printf("VM_DEBUG: Calling callback for ARR_FIXED (Key %d)\n", key);
                if (ctx->mode == CND_MODE_ENCODE) {
                     // Notify host about array start so it can push context
                     // We should probably pass u32, but for now let's cast or ensure callback handles it.
                     // The callback signature is (ctx, key, type, void*).
                     // For ARR_FIXED, we pass pointer to count.
                     SYNC_IP();
                     if (ctx->io_callback(ctx, key, opcode, &count) != CND_ERR_OK) return CND_ERR_CALLBACK;
                } else {
                     // Notify host about array start
                     SYNC_IP();
                     if (ctx->io_callback(ctx, key, opcode, &count) != CND_ERR_OK) return CND_ERR_CALLBACK;
                }

                if (count > 0) {
                    SYNC_IP();
                    if (!loop_push(ctx, ctx->ip, count)) return CND_ERR_OOB;
                } else {
                     SYNC_IP();
                     skip_loop_body(ctx);
                     RELOAD_PC();
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
                    RELOAD_PC();
                } else {
                    // Loop finished, notify callback
                    SYNC_IP();
                    if (ctx->io_callback(ctx, 0, OP_ARR_END, NULL) != CND_ERR_OK) return CND_ERR_CALLBACK;
                    loop_pop(ctx);
                }
                break;
            }

            case OP_RAW_BYTES: {
                uint16_t key = FETCH_IL_U16(ctx);
                uint32_t count = FETCH_IL_U32(ctx);
                
                if (ctx->cursor + count > ctx->data_len) return CND_ERR_OOB;
                
                // Callback receives/provides pointer to data in buffer
                // For Encode: Callback writes TO this pointer
                // For Decode: Callback reads FROM this pointer
                // Since it's raw bytes, we just pass the pointer to the buffer.
                void* ptr = ctx->data_buffer + ctx->cursor;
                
                SYNC_IP();
                if (ctx->io_callback(ctx, key, opcode, ptr) != CND_ERR_OK) return CND_ERR_CALLBACK;
                
                ctx->cursor += count;
                break;
            }

            case OP_SWITCH: {
                uint16_t key = FETCH_IL_U16(ctx);
                uint32_t table_rel_offset = FETCH_IL_U32(ctx);
                
                // IP is now at the start of the code block (immediately after SWITCH instruction)
                SYNC_IP();
                size_t code_start_ip = ctx->ip;
                size_t table_start_ip = code_start_ip + table_rel_offset;
                
                if (table_start_ip > ctx->program->bytecode_len) return CND_ERR_OOB;
                
                // Jump to table to read it
                size_t original_ip = ctx->ip;
                ctx->ip = table_start_ip;
                
                uint16_t count = read_il_u16(ctx);
                int32_t default_off = (int32_t)read_il_u32(ctx);
                
                uint64_t disc_val = 0;
                if (ctx->io_callback(ctx, key, OP_CTX_QUERY, &disc_val) != CND_ERR_OK) return CND_ERR_CALLBACK;
                
                int32_t target_off = default_off;
                bool found = false;
                
                for (uint16_t i = 0; i < count; i++) {
                    uint64_t case_val = read_il_u64(ctx);
                    int32_t case_off = (int32_t)read_il_u32(ctx);
                    
                    if (!found && disc_val == case_val) {
                        target_off = case_off;
                        found = true;
                    }
                }
                
                // Target offset is relative to code_start_ip (original_ip)
                ctx->ip = original_ip; // Restore just in case calculations need it, or just set new
                
                if (target_off < 0) {
                     if (code_start_ip < (size_t)(-target_off)) return CND_ERR_OOB;
                     ctx->ip = code_start_ip - (size_t)(-target_off);
                } else {
                     ctx->ip = code_start_ip + (size_t)target_off;
                }
                
                if (ctx->ip > ctx->program->bytecode_len) return CND_ERR_OOB;
                RELOAD_PC();
                break;
            }

            case OP_JUMP_IF_NOT: {
                int32_t offset = (int32_t)FETCH_IL_U32(ctx);
                uint64_t condition;
                if (stack_pop(ctx, &condition) != CND_ERR_OK) return CND_ERR_STACK_UNDERFLOW;
                
                if (condition == 0) {
                    // Jump
                    SYNC_IP();
                    if (offset < 0) {
                        if (ctx->ip < (size_t)(-offset)) return CND_ERR_OOB;
                        ctx->ip -= (size_t)(-offset);
                    } else {
                        ctx->ip += (size_t)offset;
                    }
                    if (ctx->ip > ctx->program->bytecode_len) return CND_ERR_OOB;
                    RELOAD_PC();
                }
                break;
            }

            case OP_JUMP: {
                int32_t offset = (int32_t)FETCH_IL_U32(ctx);
                // Offset is relative to IP *after* reading the offset (which is current ctx->ip)
                SYNC_IP();
                if (offset < 0) {
                    if (ctx->ip < (size_t)(-offset)) return CND_ERR_OOB;
                    ctx->ip -= (size_t)(-offset);
                } else {
                    ctx->ip += (size_t)offset;
                }
                if (ctx->ip > ctx->program->bytecode_len) return CND_ERR_OOB;
                RELOAD_PC();
                break;
            }

            // --- Category G: Expression Stack & ALU ---

            case OP_LOAD_CTX: {
                uint16_t key = FETCH_IL_U16(ctx);
                uint64_t val = 0;
                SYNC_IP();
                if (ctx->io_callback(ctx, key, OP_LOAD_CTX, &val) != CND_ERR_OK) return CND_ERR_CALLBACK;
                if (stack_push(ctx, val) != CND_ERR_OK) return CND_ERR_STACK_OVERFLOW;
                break;
            }

            case OP_PUSH_IMM: {
                uint64_t val = FETCH_IL_U64(ctx);
                if (stack_push(ctx, val) != CND_ERR_OK) return CND_ERR_STACK_OVERFLOW;
                break;
            }

            case OP_POP: {
                uint64_t val;
                if (stack_pop(ctx, &val) != CND_ERR_OK) return CND_ERR_STACK_UNDERFLOW;
                break;
            }

            // Bitwise
            case OP_BIT_AND: { BINARY_OP(&); break; }
            case OP_BIT_OR:  { BINARY_OP(|); break; }
            case OP_BIT_XOR: { BINARY_OP(^); break; }
            case OP_BIT_NOT: { UNARY_OP(~); break; }
            case OP_SHL:     { BINARY_OP(<<); break; }
            case OP_SHR:     { BINARY_OP(>>); break; }

            // Comparison
            case OP_EQ:  { BINARY_OP(==); break; }
            case OP_NEQ: { BINARY_OP(!=); break; }
            case OP_GT:  { BINARY_OP(>); break; }
            case OP_LT:  { BINARY_OP(<); break; }
            case OP_GTE: { BINARY_OP(>=); break; }
            case OP_LTE: { BINARY_OP(<=); break; }

            // Logical
            case OP_LOG_AND: { BINARY_OP(&&); break; }
            case OP_LOG_OR:  { BINARY_OP(||); break; }
            case OP_LOG_NOT: { UNARY_OP(!); break; }

            default:
                break;
        }
    }
    
    // fprintf(stderr, "VM_DEBUG: Execution finished. Cursor=%zu, DataLen=%zu. Returning OK.\n", ctx->cursor, ctx->data_len);
    return CND_ERR_OK;
}

