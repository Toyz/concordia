#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include "concordia.h"
#include "compiler.h"

// --- Application Data Structure ---
// Manual definition - Decoupled from Schema logic (Key IDs map to this)
typedef enum {
    STATUS_OK = 0,
    STATUS_FAIL = 1
} Status;

typedef struct {
    uint32_t magic;
    uint32_t flags_a; // Bitfield stored as full int
    bool flag_b;
    int8_t val_c;
    int64_t timestamp;
    
    struct { float x, y, z; } position;
    
    uint8_t matrix[4];
    int matrix_idx; // Iterator
    
    uint16_t points[255];
    uint8_t points_len;
    int points_idx; // Iterator
    
    char name[33];
    Status status;
    
    uint8_t confidence;
    uint16_t error_code;
    char reason[256]; // For FAIL
    
    uint8_t percentage;
    double temperature;
    
    uint8_t val_add;
    uint8_t val_sub;
    uint8_t val_mul;
    uint8_t val_div;

    double poly_val;
    double spline_val;
    uint8_t expr_val;

    struct {
        uint8_t a_3bits;
        uint8_t b_5bits;
        uint8_t c_4bits;
        uint8_t d_aligned;
    } bit_packed;

    bool has_extra;
    char extra_data[64]; // For has_extra=true
    
    uint8_t adv_mode;
    uint16_t adv_simple_val;
    bool adv_has_details;
    char adv_details[64];
    uint8_t adv_fallback_code;

    // New Features
    uint16_t dynamic_len;
    uint8_t dynamic_bytes[256];
    int dynamic_bytes_idx;

    uint8_t str_count;
    char dynamic_strings[10][64]; // Max 10 strings, 64 chars each
    int dynamic_strings_idx;

    uint8_t rest_of_stream[1024];
    size_t rest_of_stream_len;
    int rest_of_stream_idx;
} KitchenSink;

// --- IO Callback ---
// Manually written callback.
// This is "Resilient" code: It only handles keys it knows about.
// If the IL changes (new keys added), this callback simply ignores them (returns OK).
// This allows the Schema to evolve (Hot Reload) without breaking the Firmware.

cnd_error_t sink_cb(cnd_vm_ctx* ctx, uint16_t key_id, uint8_t type, void* ptr) {
    // printf("Callback: key_id=%d, type=%d\n", key_id, type);
    KitchenSink* obj = (KitchenSink*)ctx->user_ptr;
    const char* key_name = cnd_get_key_name(ctx->program, key_id); // Optional: Use name lookup for debug/robustness
    // printf("Key Name: %s\n", key_name ? key_name : "NULL");
    
    if (key_name && (strcmp(key_name, "str_count") == 0 || strcmp(key_name, "dynamic_len") == 0)) {
        printf("CB: Key=%s (%d), Type=%d, Mode=%s\n", key_name, key_id, type, ctx->mode == CND_MODE_ENCODE ? "ENC" : "DEC");
        if (type == OP_IO_U8 || type == OP_IO_U16) {
             if (ctx->mode == CND_MODE_DECODE) {
                 // We are about to read into obj->field. The value in *ptr is what was read from stream.
                 // Wait, IO_VAL does: obj->field = *(ctype*)ptr;
                 // So we can print *ptr
                 if (type == OP_IO_U8) printf("  Reading Value: %d\n", *(uint8_t*)ptr);
                 if (type == OP_IO_U16) printf("  Reading Value: %d\n", *(uint16_t*)ptr);
             } else {
                 // Encode
                 if (type == OP_IO_U8) printf("  Writing Value: %d\n", obj->str_count); // Assuming str_count for now
             }
        }
        if (type == OP_CTX_QUERY) {
             // We are about to return the value in *ptr
             // But we haven't set it yet.
             // The code below sets it.
        }
    }

    // Helper macros
    #define ENCODE (ctx->mode == CND_MODE_ENCODE)
    #define DECODE (ctx->mode == CND_MODE_DECODE)
    #define IO_VAL(ctype, field) { if (ENCODE) *(ctype*)ptr = (ctype)obj->field; else obj->field = *(ctype*)ptr; }
    
    // Debug
    if (!key_name) {
        return CND_ERR_OK; // Safe fallback
    }

    // Control Flow
    if (type == OP_CTX_QUERY || type == OP_LOAD_CTX) {
        // Discriminators for switch/if
        if (strcmp(key_name, "status") == 0) *(uint64_t*)ptr = obj->status;
        else if (strcmp(key_name, "has_extra") == 0) *(uint64_t*)ptr = obj->has_extra;
        else if (strcmp(key_name, "adv_mode") == 0) *(uint64_t*)ptr = obj->adv_mode;
        else if (strcmp(key_name, "adv_has_details") == 0) *(uint64_t*)ptr = obj->adv_has_details;
        else if (strcmp(key_name, "val_add") == 0) *(uint64_t*)ptr = obj->val_add;
        else if (strcmp(key_name, "val_sub") == 0) *(uint64_t*)ptr = obj->val_sub;
        else if (strcmp(key_name, "dynamic_len") == 0) *(uint64_t*)ptr = obj->dynamic_len;
        else if (strcmp(key_name, "str_count") == 0) *(uint64_t*)ptr = obj->str_count;
        return CND_ERR_OK;
    }

    // Array Loops
    if (type == OP_ARR_FIXED) {
        if (strcmp(key_name, "matrix") == 0) obj->matrix_idx = 0;
        return CND_ERR_OK;
    }
    if (type == OP_ARR_PRE_U8) { // points
        if (strcmp(key_name, "points") == 0) {
            obj->points_idx = 0;
            IO_VAL(uint8_t, points_len);
        }
        return CND_ERR_OK;
    }
    if (type == OP_ARR_DYNAMIC) {
        if (strcmp(key_name, "dynamic_bytes") == 0) obj->dynamic_bytes_idx = 0;
        else if (strcmp(key_name, "dynamic_strings") == 0) obj->dynamic_strings_idx = 0;
        return CND_ERR_OK;
    }
    if (type == OP_ARR_EOF) {
        if (strcmp(key_name, "rest_of_stream") == 0) {
            obj->rest_of_stream_idx = 0;
            if (DECODE) obj->rest_of_stream_len = 0;
        }
        return CND_ERR_OK;
    }
    if (type == OP_ARR_END) return CND_ERR_OK;
    if (type == OP_ENTER_STRUCT || type == OP_EXIT_STRUCT) return CND_ERR_OK;

    // Fields by Name (String lookup is slow but robust for demo; embedded would use Switch on ID)
    if (!key_name) return CND_ERR_OK;

    if (strcmp(key_name, "magic") == 0) IO_VAL(uint32_t, magic)
    else if (strcmp(key_name, "flags_a") == 0) { // Bitfield U -> uint64_t ptr
        if (ENCODE) *(uint64_t*)ptr = obj->flags_a; else obj->flags_a = (uint32_t)*(uint64_t*)ptr;
    }
    else if (strcmp(key_name, "flag_b") == 0) {
        if (ENCODE) *(uint8_t*)ptr = obj->flag_b; else obj->flag_b = *(uint8_t*)ptr;
    }
    else if (strcmp(key_name, "val_c") == 0) { // Bitfield I -> int64_t ptr
        if (ENCODE) *(int64_t*)ptr = obj->val_c; else obj->val_c = (int8_t)*(int64_t*)ptr;
    }
    else if (strcmp(key_name, "timestamp") == 0) IO_VAL(int64_t, timestamp)
    else if (strcmp(key_name, "x") == 0) IO_VAL(float, position.x)
    else if (strcmp(key_name, "y") == 0) IO_VAL(float, position.y)
    else if (strcmp(key_name, "z") == 0) IO_VAL(float, position.z)
    else if (strcmp(key_name, "matrix") == 0 && type == OP_IO_U8) {
        if (obj->matrix_idx < 4) {
            IO_VAL(uint8_t, matrix[obj->matrix_idx]);
            obj->matrix_idx++;
        }
    }
    else if (strcmp(key_name, "points") == 0 && type == OP_IO_U16) {
        if (obj->points_idx < obj->points_len) {
            IO_VAL(uint16_t, points[obj->points_idx]);
            obj->points_idx++;
        }
    }
    else if (strcmp(key_name, "dynamic_len") == 0) IO_VAL(uint16_t, dynamic_len)
    else if (strcmp(key_name, "dynamic_bytes") == 0 && type == OP_IO_U8) {
        if (obj->dynamic_bytes_idx < obj->dynamic_len) {
            IO_VAL(uint8_t, dynamic_bytes[obj->dynamic_bytes_idx]);
            obj->dynamic_bytes_idx++;
        }
    }
    else if (strcmp(key_name, "str_count") == 0) IO_VAL(uint8_t, str_count)
    else if (strcmp(key_name, "dynamic_strings") == 0) {
        if (obj->dynamic_strings_idx < obj->str_count) {
            if (ENCODE) *(const char**)ptr = obj->dynamic_strings[obj->dynamic_strings_idx];
            else {
                uint8_t len = ((uint8_t*)ptr)[-1]; // Get length from prefix
                if (len >= 64) len = 63;
                strncpy(obj->dynamic_strings[obj->dynamic_strings_idx], (const char*)ptr, len);
                obj->dynamic_strings[obj->dynamic_strings_idx][len] = 0;
            }
            obj->dynamic_strings_idx++;
        }
    }
    else if (strcmp(key_name, "rest_of_stream") == 0 && type == OP_IO_U8) {
        if (ENCODE) {
            if ((size_t)obj->rest_of_stream_idx < obj->rest_of_stream_len) {
                *(uint8_t*)ptr = obj->rest_of_stream[obj->rest_of_stream_idx];
                obj->rest_of_stream_idx++;
            }
        } else {
            if (obj->rest_of_stream_len < 1024) {
                obj->rest_of_stream[obj->rest_of_stream_len] = *(uint8_t*)ptr;
                obj->rest_of_stream_len++;
            }
        }
    }
    else if (strcmp(key_name, "name") == 0) {
        if (ENCODE) *(const char**)ptr = obj->name;
        else {
            strncpy(obj->name, (const char*)ptr, 32);
            obj->name[32] = 0;
        }
    }
    else if (strcmp(key_name, "status") == 0) {
        if (ENCODE) *(uint8_t*)ptr = obj->status;
        else obj->status = (Status)*(uint8_t*)ptr;
    }
    else if (strcmp(key_name, "confidence") == 0) IO_VAL(uint8_t, confidence)
    else if (strcmp(key_name, "error_code") == 0) IO_VAL(uint16_t, error_code)
    else if (strcmp(key_name, "reason") == 0) {
        if (ENCODE) *(const char**)ptr = obj->reason;
        else { strncpy(obj->reason, (const char*)ptr, 255); obj->reason[255] = 0; }
    }
    else if (strcmp(key_name, "percentage") == 0) IO_VAL(uint8_t, percentage)
    else if (strcmp(key_name, "temperature") == 0) {
        // Scaled: VM passes OP_IO_F64 (double)
        if (ENCODE) *(double*)ptr = obj->temperature;
        else obj->temperature = *(double*)ptr;
    }
    else if (strcmp(key_name, "val_add") == 0) IO_VAL(uint8_t, val_add)
    else if (strcmp(key_name, "val_sub") == 0) IO_VAL(uint8_t, val_sub)
    else if (strcmp(key_name, "val_mul") == 0) IO_VAL(uint8_t, val_mul)
    else if (strcmp(key_name, "val_div") == 0) IO_VAL(uint8_t, val_div)
    else if (strcmp(key_name, "poly_val") == 0) {
        if (ENCODE) *(double*)ptr = obj->poly_val;
        else obj->poly_val = *(double*)ptr;
    }
    else if (strcmp(key_name, "spline_val") == 0) {
        if (ENCODE) *(double*)ptr = obj->spline_val;
        else obj->spline_val = *(double*)ptr;
    }
    else if (strcmp(key_name, "expr_val") == 0) {
        if (ENCODE) *(uint8_t*)ptr = obj->expr_val;
        else obj->expr_val = *(uint8_t*)ptr;
    }
    else if (strcmp(key_name, "a_3bits") == 0) {
        if (ENCODE) *(uint8_t*)ptr = obj->bit_packed.a_3bits; else obj->bit_packed.a_3bits = *(uint8_t*)ptr;
    }
    else if (strcmp(key_name, "b_5bits") == 0) {
        if (ENCODE) *(uint8_t*)ptr = obj->bit_packed.b_5bits; else obj->bit_packed.b_5bits = *(uint8_t*)ptr;
    }
    else if (strcmp(key_name, "c_4bits") == 0) {
        if (ENCODE) *(uint8_t*)ptr = obj->bit_packed.c_4bits; else obj->bit_packed.c_4bits = *(uint8_t*)ptr;
    }
    else if (strcmp(key_name, "d_aligned") == 0) {
        if (ENCODE) *(uint8_t*)ptr = obj->bit_packed.d_aligned; else obj->bit_packed.d_aligned = *(uint8_t*)ptr;
    }
    else if (strcmp(key_name, "has_extra") == 0) {
        if (ENCODE) *(uint8_t*)ptr = obj->has_extra; else obj->has_extra = *(uint8_t*)ptr;
    }
    else if (strcmp(key_name, "extra_data") == 0) {
        if (ENCODE) *(const char**)ptr = obj->extra_data;
        else { 
            uint8_t len = ((uint8_t*)ptr)[-1]; // Get length from prefix
            if (len >= sizeof(obj->extra_data)) len = sizeof(obj->extra_data) - 1; // Cap to buffer size
            strncpy(obj->extra_data, (const char*)ptr, len); 
            obj->extra_data[len] = 0; // Null-terminate
        }
    }
    else if (strcmp(key_name, "adv_mode") == 0) IO_VAL(uint8_t, adv_mode)
    else if (strcmp(key_name, "adv_simple_val") == 0) IO_VAL(uint16_t, adv_simple_val)
    else if (strcmp(key_name, "adv_has_details") == 0) {
        if (ENCODE) *(uint8_t*)ptr = obj->adv_has_details; else obj->adv_has_details = *(uint8_t*)ptr;
    }
    else if (strcmp(key_name, "adv_details") == 0) {
        if (ENCODE) *(const char**)ptr = obj->adv_details;
        else { 
            uint8_t len = ((uint8_t*)ptr)[-1]; // Get length from prefix
            if (len >= sizeof(obj->adv_details)) len = sizeof(obj->adv_details) - 1; // Cap to buffer size
            strncpy(obj->adv_details, (const char*)ptr, len); 
            obj->adv_details[len] = 0; // Null-terminate
        }
    }
    else if (strcmp(key_name, "adv_fallback_code") == 0) IO_VAL(uint8_t, adv_fallback_code)

    return CND_ERR_OK;
}

int main(void) {
    printf("=== Concordia Kitchen Sink (Manual Binding) ===\n");
    
    // 1. Compile
    printf("Compiling schema...\n");
    if (cnd_compile_file("examples/kitchen_sink/kitchen_sink.cnd", "kitchen_sink.il", 0, 0) != 0) {
        printf("Compile failed\n"); return 1;
    }
    
    // 2. Load IL
    FILE* f = fopen("kitchen_sink.il", "rb");
    fseek(f, 0, SEEK_END); long size = ftell(f); fseek(f, 0, SEEK_SET);
    uint8_t* il = malloc(size); fread(il, 1, size, f); fclose(f);
    printf("IL loaded, size: %ld\n", size);
    cnd_program prog;
    if (cnd_program_load_il(&prog, il, size) != CND_ERR_OK) { printf("Load failed\n"); return 1; }
    printf("Program loaded\n");
    
    // 3. Data
    KitchenSink data = {
        .magic = 0xCAFEBABE,
        .flags_a = 1, .flag_b = true, .val_c = -5,
        .timestamp = 123456789,
        .position = { 1.0f, 2.0f, 3.0f },
        .matrix = {1, 2, 3, 4},
        .points_len = 3, .points = {10, 20, 30},
        .status = STATUS_OK, .confidence = 100,
        .percentage = 50, .temperature = 25.5,
        .val_add = 10, .val_sub = 20, .val_mul = 5, .val_div = 40,
        .poly_val = 75.0, // 5 + 2(10) + 0.5(100) = 75.0. Raw should be 10.
        .spline_val = 50.0, // Raw 5 -> 50.0 (Segment 1)
        .bit_packed = { .a_3bits = 7, .b_5bits = 31, .c_4bits = 15, .d_aligned = 255 },
        .has_extra = true,
        .adv_mode = 0, .adv_simple_val = 777,
        .dynamic_len = 5, .dynamic_bytes = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE},
        .str_count = 2,
        .rest_of_stream = {0xDE, 0xAD, 0xBE, 0xEF}, .rest_of_stream_len = 4
    };
    strcpy(data.name, "Manual Demo");
    strcpy(data.extra_data, "Manual Extra");
    strcpy(data.dynamic_strings[0], "Hello");
    strcpy(data.dynamic_strings[1], "World");
    
    // 4. Encode
    printf("Starting Encode...\n");
    uint8_t buffer[1024]; memset(buffer, 0, 1024);
    cnd_vm_ctx ctx;
    cnd_init(&ctx, CND_MODE_ENCODE, &prog, buffer, 1024, sink_cb, &data);
    int err = cnd_execute(&ctx);
    if (err != CND_ERR_OK) { printf("Encode failed with error %d\n", err); return 1; }
    printf("Encoded %zu bytes\n", ctx.cursor);
    
    // 5. Decode
    printf("Starting Decode...\n");
    KitchenSink out = {0};
    cnd_init(&ctx, CND_MODE_DECODE, &prog, buffer, ctx.cursor, sink_cb, &out);
    if (cnd_execute(&ctx) != CND_ERR_OK) { printf("Decode failed\n"); return 1; }
    
    printf("Decoded Results:\n");
    printf("  Magic: 0x%X\n", out.magic);
    printf("  Flags A: %u, Flag B: %s, Val C: %d\n", out.flags_a, out.flag_b ? "true" : "false", out.val_c);
    printf("  Timestamp: %lld\n", (long long)out.timestamp);
    printf("  Position: { %.2f, %.2f, %.2f }\n", out.position.x, out.position.y, out.position.z);
    printf("  Matrix: [%d, %d, %d, %d]\n", out.matrix[0], out.matrix[1], out.matrix[2], out.matrix[3]);
    printf("  Points (%d): [", out.points_len);
    for(int i=0; i<out.points_len; i++) printf("%d%s", out.points[i], i==out.points_len-1 ? "" : ", ");
    printf("]\n");
    printf("  Name: %s\n", out.name);
    printf("  Status: %d\n", out.status);
    printf("  Confidence: %d, Error: %d, Reason: %s\n", out.confidence, out.error_code, out.reason);
    printf("  Percentage: %d%%, Temp: %.2f\n", out.percentage, out.temperature);
    printf("  Val Add: %d, Sub: %d, Mul: %d, Div: %d\n", out.val_add, out.val_sub, out.val_mul, out.val_div);
    printf("  Poly Val: %.2f\n", out.poly_val);
    printf("  Spline Val: %.2f\n", out.spline_val);
    printf("  Expr Val: %d\n", out.expr_val);
    printf("  BitPacked: A=%d, B=%d, C=%d, D=%d\n", out.bit_packed.a_3bits, out.bit_packed.b_5bits, out.bit_packed.c_4bits, out.bit_packed.d_aligned);
    printf("  Has Extra: %s\n", out.has_extra ? "true" : "false");
    if (out.has_extra) printf("    Extra Data: %s\n", out.extra_data);
    printf("  Adv Mode: %d\n", out.adv_mode);
    printf("  Adv Simple Val: %d\n", out.adv_simple_val);
    printf("  Adv Has Details: %s\n", out.adv_has_details ? "true" : "false");
    if (out.adv_has_details) printf("    Adv Details: %s\n", out.adv_details);
    printf("  Adv Fallback: %d\n", out.adv_fallback_code);
    printf("  Dynamic Bytes (%d): [", out.dynamic_len);
    for(int i=0; i<out.dynamic_len; i++) printf("0x%02X%s", out.dynamic_bytes[i], i==out.dynamic_len-1 ? "" : ", ");
    printf("]\n");
    printf("  Dynamic Strings (%d): [", out.str_count);
    for(int i=0; i<out.str_count; i++) printf("\"%s\"%s", out.dynamic_strings[i], i==out.str_count-1 ? "" : ", ");
    printf("]\n");
    printf("  Rest of Stream (%zu): [", out.rest_of_stream_len);
    for(size_t i=0; i<out.rest_of_stream_len; i++) printf("0x%02X%s", out.rest_of_stream[i], i==out.rest_of_stream_len-1 ? "" : ", ");
    printf("]\n");
    
    free(il);
    return 0;
}
