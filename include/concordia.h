#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifndef CONCORDIA_H
#define CONCORDIA_H

#ifdef __cplusplus
extern "C" {
#endif

// --- 1. Opcodes (Category A-F) ---

// Category A: Meta & State
#define OP_NOOP             0x00
#define OP_SET_ENDIAN_LE    0x01
#define OP_SET_ENDIAN_BE    0x02
#define OP_ENTER_STRUCT     0x03
#define OP_EXIT_STRUCT      0x04
#define OP_META_VERSION     0x05
#define OP_CTX_QUERY        0x06
#define OP_META_NAME        0x07

// Category B: Primitives (Byte Aligned)
#define OP_IO_U8            0x10
#define OP_IO_U16           0x11
#define OP_IO_U32           0x12
#define OP_IO_U64           0x13
#define OP_IO_I8            0x14
#define OP_IO_I16           0x15
#define OP_IO_I32           0x16
#define OP_IO_I64           0x17
#define OP_IO_F32           0x18
#define OP_IO_F64           0x19
#define OP_IO_BOOL          0x1A

// Category C: Bitfields & Padding
#define OP_IO_BIT_U         0x20
#define OP_IO_BIT_I         0x21
#define OP_IO_BIT_BOOL      0x22
#define OP_ALIGN_PAD        0x23
#define OP_ALIGN_FILL       0x24

// Category D: Arrays & Strings
#define OP_STR_NULL         0x30
#define OP_STR_PRE_U8       0x31
#define OP_STR_PRE_U16      0x32
#define OP_STR_PRE_U32      0x33
#define OP_ARR_FIXED        0x34
#define OP_ARR_PRE_U8       0x35
#define OP_ARR_PRE_U16      0x36
#define OP_ARR_PRE_U32      0x37
#define OP_ARR_END          0x38
#define OP_RAW_BYTES        0x39

// Category E: Validation
#define OP_CONST_CHECK      0x40
#define OP_CONST_WRITE      0x41
#define OP_RANGE_CHECK      0x42
#define OP_SCALE_LIN        0x43
#define OP_CRC_16           0x44
#define OP_TRANS_ADD        0x45
#define OP_TRANS_SUB        0x46
#define OP_TRANS_MUL        0x47
#define OP_TRANS_DIV        0x48
#define OP_CRC_32           0x49
#define OP_MARK_OPTIONAL    0x4A
#define OP_ENUM_CHECK       0x4B
#define OP_TRANS_POLY       0x4C
#define OP_TRANS_SPLINE     0x4D

// Category F: Control Flow
#define OP_JUMP_IF_NOT      0x50
#define OP_SWITCH           0x51
#define OP_JUMP             0x52

// Category G: Expression Stack & ALU
#define OP_LOAD_CTX         0x60
#define OP_PUSH_IMM         0x61
#define OP_POP              0x62

// Bitwise
#define OP_BIT_AND          0x63
#define OP_BIT_OR           0x64
#define OP_BIT_XOR          0x65
#define OP_BIT_NOT          0x66
#define OP_SHL              0x67
#define OP_SHR              0x68

// Comparison
#define OP_EQ               0x69
#define OP_NEQ              0x6A
#define OP_GT               0x6B
#define OP_LT               0x6C
#define OP_GTE              0x6D
#define OP_LTE              0x6E

// Logical
#define OP_LOG_AND          0x6F
#define OP_LOG_OR           0x70
#define OP_LOG_NOT          0x71

// --- 2. VM Context ---

typedef enum {
    CND_ERR_OK = 0,
    CND_ERR_OOB = 1,           // Out of bounds (IL or Data)
    CND_ERR_INVALID_OP = 2,    // Unknown Opcode
    CND_ERR_VALIDATION = 3,    // Range/Const check failed
    CND_ERR_CALLBACK = 4,      // User callback returned error
    CND_ERR_STACK_OVERFLOW = 5,
    CND_ERR_STACK_UNDERFLOW = 6
} cnd_error_t;

typedef enum {
    CND_MODE_ENCODE = 0, // Map -> Binary
    CND_MODE_DECODE = 1  // Binary -> Map
} cnd_mode_t;

typedef enum {
    CND_LE = 0, // Little Endian
    CND_BE = 1  // Big Endian
} cnd_endian_t;

typedef enum {
    CND_TRANS_NONE = 0,
    CND_TRANS_SCALE_F64, // Linear Float Scale (y = mx + c)
    CND_TRANS_ADD_I64,
    CND_TRANS_SUB_I64,
    CND_TRANS_MUL_I64,
    CND_TRANS_DIV_I64,
    CND_TRANS_POLY,
    CND_TRANS_SPLINE
} cnd_trans_t;

#define CND_MAX_LOOP_DEPTH 8
#define CND_MAX_EXPR_STACK 8

typedef struct {
    size_t start_ip;
    uint32_t remaining;
} cnd_loop_frame;

// Forward declaration for the IO callback
struct cnd_vm_ctx_t;

// Callback for reading/writing data to/from the Host's "Map"
// In a real embedded system, this might look up offsets in a struct.
// For JSON/Debug, it might look up string keys.
typedef cnd_error_t (*cnd_io_cb)(
    struct cnd_vm_ctx_t* ctx,
    uint16_t key_id,
    uint8_t type_opcode, // The opcode triggering this IO (e.g. OP_IO_U8)
    void* data_ptr       // Pointer to read from or write to
);

typedef struct {
    const uint8_t* bytecode;    // Pointer to IL Bytecode
    size_t bytecode_len;        // Length of IL Bytecode
    const char* string_table;   // Pointer to string table (packed null-terminated strings)
    uint16_t string_count;      // Number of strings in the table
} cnd_program;

typedef struct cnd_vm_ctx_t {
    // --- Configuration ---
    cnd_mode_t mode;            // Operation mode
    const cnd_program* program; // Pointer to the program being executed
    
    uint8_t* data_buffer;       // Pointer to Binary Data Buffer (Read for Decode, Write for Encode)
    size_t data_len;            // Total size of data buffer (for bounds checking)
    
    cnd_io_cb io_callback;      // Host callback for mapping KeyIDs to values
    void* user_ptr;             // User context for the callback

    // --- Runtime State ---
    size_t ip;                  // Instruction Pointer (Index into il_code)
    size_t cursor;              // Data Cursor (Index into data_buffer)
    uint8_t bit_offset;         // 0-7, for bitfields
    cnd_endian_t endianness;    // Current Endianness state
    
    // Scaling / Transformation State (reset after each IO)
    cnd_trans_t trans_type;
    double trans_f_factor;
    double trans_f_offset;
    int64_t trans_i_val;
    
    // Polynomial State
    const uint8_t* trans_poly_data; // Pointer to doubles in bytecode
    uint8_t trans_poly_count;

    // Spline State
    const uint8_t* trans_spline_data; // Pointer to (double, double) pairs in bytecode
    uint8_t trans_spline_count;

    bool is_next_optional;      // If true, OOB reads return 0 instead of error

    cnd_loop_frame loop_stack[CND_MAX_LOOP_DEPTH];
    uint8_t loop_depth;

    uint64_t expr_stack[CND_MAX_EXPR_STACK];
    uint8_t expr_sp;
} cnd_vm_ctx;

// --- 3. Public API ---

/**
 * Load a program from a byte array (Raw Bytecode).
 * This does not copy data; it just sets up the program struct.
 * String table will be empty.
 */
void cnd_program_load(cnd_program* program, const uint8_t* bytecode, size_t len);

/**
 * Load a program from a full IL binary image (Header + Strings + Bytecode).
 * Parses the header to locate bytecode and string table.
 * Returns CND_ERR_OK on success, or CND_ERR_INVALID_OP if header is invalid.
 */
cnd_error_t cnd_program_load_il(cnd_program* program, const uint8_t* image, size_t len);

/**
 * Get the string name for a given Key ID.
 * Returns NULL if key_id is out of range or string table is missing.
 */
const char* cnd_get_key_name(const cnd_program* program, uint16_t key_id);

/**
 * Initialize a VM context.
 */
void cnd_init(cnd_vm_ctx* ctx,
              cnd_mode_t mode,
              const cnd_program* program,
              uint8_t* data, size_t data_len,
              cnd_io_cb cb, void* user);

/**
 * Execute the VM until completion or error.
 */
cnd_error_t cnd_execute(cnd_vm_ctx* ctx);

#ifdef __cplusplus
}
#endif

#endif // CONCORDIA_H
