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
                uint16_t key = read_il_u16(ctx);
                uint8_t type = read_il_u8(ctx);
                uint64_t expected = 0;
                int size = 0;
                if (type == OP_IO_U8 || type == OP_IO_I8) { expected = read_il_u8(ctx); size=1; }
                else if (type == OP_IO_U16 || type == OP_IO_I16) { expected = read_il_u16(ctx); size=2; }
                else if (type == OP_IO_U32 || type == OP_IO_I32) { expected = read_il_u32(ctx); size=4; }
                else if (type == OP_IO_U64 || type == OP_IO_I64) { expected = read_il_u64(ctx); size=8; }
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
                    if (size == 1) { uint8_t v = (uint8_t)actual; if (ctx->io_callback(ctx, key, type, &v) != CND_ERR_OK) return CND_ERR_CALLBACK; }
                    else if (size == 2) { uint16_t v = (uint16_t)actual; if (ctx->io_callback(ctx, key, type, &v) != CND_ERR_OK) return CND_ERR_CALLBACK; }
                    else if (size == 4) { uint32_t v = (uint32_t)actual; if (ctx->io_callback(ctx, key, type, &v) != CND_ERR_OK) return CND_ERR_CALLBACK; }
                    else if (size == 8) { uint64_t v = actual; if (ctx->io_callback(ctx, key, type, &v) != CND_ERR_OK) return CND_ERR_CALLBACK; }
                }
                
                ctx->cursor += size;
                break;
            }

            case OP_ENUM_CHECK: {
                uint8_t type = read_il_u8(ctx);
                uint16_t count = read_il_u16(ctx);
                bool found = false;
                
                #define CHECK_ENUM(size, ctype, READ_EXPR) \
                    ctype actual = (READ_EXPR); \
                    for (uint16_t i = 0; i < count; i++) { \
                        ctype val = 0; \
                        if (size == 1) val = (ctype)read_il_u8(ctx); \
                        else if (size == 2) val = (ctype)read_il_u16(ctx); \
                        else if (size == 4) val = (ctype)read_il_u32(ctx); \
                        else if (size == 8) val = (ctype)read_il_u64(ctx); \
                        if (actual == val) { \
                            found = true; \
                            ctx->ip += (count - 1 - i) * size; \
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
                    if (!loop_push(ctx, ctx->ip, count)) return CND_ERR_OOB;
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
                    if (!loop_push(ctx, ctx->ip, count)) return CND_ERR_OOB;
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
                    if (!loop_push(ctx, ctx->ip, count)) return CND_ERR_OOB;
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
                uint32_t count = read_il_u32(ctx);
                // printf("VM_DEBUG: Calling callback for ARR_FIXED (Key %d)\n", key);
                if (ctx->mode == CND_MODE_ENCODE) {
                     // Notify host about array start so it can push context
                     uint16_t c = (uint16_t)count; // Callback expects u16? No, ptr to void.
                     // We should probably pass u32, but for now let's cast or ensure callback handles it.
                     // The callback signature is (ctx, key, type, void*).
                     // For ARR_FIXED, we pass pointer to count.
                     if (ctx->io_callback(ctx, key, opcode, &count) != CND_ERR_OK) return CND_ERR_CALLBACK;
                } else {
                     // Notify host about array start
                     if (ctx->io_callback(ctx, key, opcode, &count) != CND_ERR_OK) return CND_ERR_CALLBACK;
                }

                if (count > 0) {
                    if (!loop_push(ctx, ctx->ip, count)) return CND_ERR_OOB;
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

