#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include "concordia.h"
#include "compiler.h" // Fix implicit declaration

// --- C Struct Definition ---

typedef enum { STATUS_OK = 0, STATUS_FAIL = 1 } Status;

typedef struct {
    float x, y, z;
} Vec3;

typedef struct {
    uint32_t magic;
    
    // Bitfields stored in host struct
    uint16_t flags_a;
    bool flag_b;
    int8_t val_c;
    
    int64_t timestamp;
    
    Vec3 position;
    
    uint8_t matrix[4];
    int matrix_idx; // Helper for iteration
    
    uint8_t points_len;
    uint16_t points[255];
    int points_idx; // Helper
    
    char name[33];
    
    Status status;
    
    // Union fields
    uint8_t confidence;
    uint16_t error_code;
    char fail_reason[256];
    
    uint8_t percentage;
    double temperature; // Scaled
    
    bool has_extra;
    char extra_data[64];

    uint32_t checksum;
    // Advanced Switch with If/Else
    uint8_t adv_mode;
    uint16_t adv_simple_val;
    bool adv_has_details;
    char adv_details[64];
    uint8_t adv_fallback_code;
} KitchenSink;

// --- IO Callback ---

cnd_error_t sink_cb(cnd_vm_ctx* ctx, uint16_t key, uint8_t type, void* ptr) {
    KitchenSink* obj = (KitchenSink*)ctx->user_ptr;
    const char* key_name = cnd_get_key_name(ctx->program, key);
    
    // Helper for encoding vs decoding assignment (Removed typeof)
    #define IO_VAL(ctype, field) if(ctx->mode==CND_MODE_ENCODE) *(ctype*)ptr = (ctype)obj->field; else obj->field = *(ctype*)ptr;
    #define IO_FLOAT(ctype, field) if(ctx->mode==CND_MODE_ENCODE) *(ctype*)ptr = (ctype)obj->field; else obj->field = (double)*(ctype*)ptr;
    
    // Control flow queries (Switch & If)
    if (type == OP_CTX_QUERY || type == OP_LOAD_CTX) {
        if (strcmp(key_name, "status") == 0) {
            *(uint64_t*)ptr = (uint64_t)obj->status;
            return CND_ERR_OK;
        }
        if (strcmp(key_name, "has_extra") == 0) {
            *(uint8_t*)ptr = obj->has_extra ? 1 : 0;
            return CND_ERR_OK;
        }
        if (strcmp(key_name, "adv_mode") == 0) {
            *(uint64_t*)ptr = (uint64_t)obj->adv_mode;
            return CND_ERR_OK;
        }
        if (strcmp(key_name, "adv_has_details") == 0) {
            *(uint64_t*)ptr = obj->adv_has_details ? 1 : 0;
            return CND_ERR_OK;
        }
        return CND_ERR_INVALID_OP;
    }

    // Array Counters / Init
    if (type == OP_ARR_FIXED) {
        if (strcmp(key_name, "matrix") == 0) {
            obj->matrix_idx = 0;
            // Fixed array count is in schema, we just return OK
        }
        return CND_ERR_OK;
    }
    
    if (type == OP_ARR_PRE_U8) {
        if (strcmp(key_name, "points") == 0) {
            obj->points_idx = 0;
            IO_VAL(uint8_t, points_len);
        }
        return CND_ERR_OK;
    }
    
    if (type == OP_ARR_END) return CND_ERR_OK;
    if (type == OP_ENTER_STRUCT || type == OP_EXIT_STRUCT) return CND_ERR_OK;

    // Fields
    if (strcmp(key_name, "magic") == 0) { IO_VAL(uint32_t, magic); }
    else if (strcmp(key_name, "flags_a") == 0) { IO_VAL(uint64_t, flags_a); } // Bitfield U -> uint64_t* ptr in VM
    else if (strcmp(key_name, "flag_b") == 0) { IO_VAL(uint8_t, flag_b); } // Bit boolean -> uint8_t* ptr
    else if (strcmp(key_name, "val_c") == 0) { IO_VAL(int64_t, val_c); } // Bitfield I -> int64_t* ptr
    else if (strcmp(key_name, "timestamp") == 0) { IO_VAL(int64_t, timestamp); }
    else if (strcmp(key_name, "x") == 0) { IO_VAL(float, position.x); }
    else if (strcmp(key_name, "y") == 0) { IO_VAL(float, position.y); }
    else if (strcmp(key_name, "z") == 0) { IO_VAL(float, position.z); }
    else if (strcmp(key_name, "matrix") == 0) {
        if (type == OP_IO_U8) {
            if (obj->matrix_idx < 4) {
                IO_VAL(uint8_t, matrix[obj->matrix_idx]);
                obj->matrix_idx++;
            }
        }
    }
    else if (strcmp(key_name, "points") == 0) {
        if (type == OP_IO_U16) {
            if (obj->points_idx < obj->points_len) {
                IO_VAL(uint16_t, points[obj->points_idx]);
                obj->points_idx++;
            }
        }
    }
    else if (strcmp(key_name, "name") == 0) {
        if (ctx->mode == CND_MODE_ENCODE) {
            *(const char**)ptr = obj->name;
        } else {
            // Copy from buffer ptr to struct
            const char* src = (const char*)ptr;
            // Safe copy with null termination limit
            #ifdef _MSC_VER
            strncpy_s(obj->name, sizeof(obj->name), src, 32);
            #else
            strncpy(obj->name, src, 32);
            #endif
            obj->name[32] = 0;
        }
    }
    else if (strcmp(key_name, "status") == 0) { 
        if(ctx->mode==CND_MODE_ENCODE) *(uint8_t*)ptr = (uint8_t)obj->status; 
        else {
            uint8_t val = *(uint8_t*)ptr;
            obj->status = (Status)val;
        }
    }
    else if (strcmp(key_name, "confidence") == 0) { IO_VAL(uint8_t, confidence); }
    else if (strcmp(key_name, "error_code") == 0) { IO_VAL(uint16_t, error_code); }
    else if (strcmp(key_name, "reason") == 0) {
        if (ctx->mode == CND_MODE_ENCODE) {
            *(const char**)ptr = obj->fail_reason;
        } else {
            const char* src = (const char*)ptr;
            #ifdef _MSC_VER
            strncpy_s(obj->fail_reason, sizeof(obj->fail_reason), src, 255);
            #else
            strncpy(obj->fail_reason, src, 255);
            #endif
            obj->fail_reason[255] = 0;
        }
    }
    else if (strcmp(key_name, "percentage") == 0) { IO_VAL(uint8_t, percentage); }
    else if (strcmp(key_name, "temperature") == 0) { 
        // type is OP_IO_F64 because @scale converts it
        IO_FLOAT(double, temperature); 
    }
    else if (strcmp(key_name, "has_extra") == 0) { IO_VAL(uint8_t, has_extra); }
    else if (strcmp(key_name, "extra_data") == 0) {
        if (ctx->mode == CND_MODE_ENCODE) {
            *(const char**)ptr = obj->extra_data;
        } else {
            const char* src = (const char*)ptr;
            size_t len = 0;
            // Hack: Read prefix to get length because VM doesn't pass it
            if (type == OP_STR_PRE_U8) len = *(const uint8_t*)(src - 1);
            
            if (len > 63) len = 63;
            memcpy(obj->extra_data, src, len);
            obj->extra_data[len] = 0;
        }
    }
    else if (strcmp(key_name, "checksum") == 0) { IO_VAL(uint32_t, checksum); }
    // Advanced Switch with If/Else
    else if (strcmp(key_name, "adv_mode") == 0) { IO_VAL(uint8_t, adv_mode); }
    else if (strcmp(key_name, "adv_simple_val") == 0) { IO_VAL(uint16_t, adv_simple_val); }
    else if (strcmp(key_name, "adv_has_details") == 0) { IO_VAL(uint8_t, adv_has_details); }
    else if (strcmp(key_name, "adv_details") == 0) {
        if (ctx->mode == CND_MODE_ENCODE) {
            *(const char**)ptr = obj->adv_details;
        } else {
            const char* src = (const char*)ptr;
            size_t len = 0;
            if (type == OP_STR_PRE_U8) len = *(const uint8_t*)(src - 1);
            if (len > 63) len = 63;
            memcpy(obj->adv_details, src, len);
            obj->adv_details[len] = 0;
        }
    }
    else if (strcmp(key_name, "adv_fallback_code") == 0) { IO_VAL(uint8_t, adv_fallback_code); }
    else {
        printf("Warning: Unknown key '%s' (type=%u)\n", key_name, type);
        // For development, treat unknown keys as OK to avoid hard failures
        return CND_ERR_OK;
    }

    return CND_ERR_OK;
}

// --- Main ---

int main() {
    cnd_error_t err;
    
    printf("=== Concordia Kitchen Sink Demo ===\n");

    // 1. Compile Schema (Runtime compilation for demo)
    printf("Compiling schema...\n");
    if (cnd_compile_file("examples/kitchen_sink/kitchen_sink.cnd", "kitchen_sink.il", 0) != 0) {
        printf("Compilation failed!\n");
        return 1;
    }

    // 2. Load Program
    FILE* f = fopen("kitchen_sink.il", "rb");
    fseek(f, 0, SEEK_END); long size = ftell(f); fseek(f, 0, SEEK_SET);
    uint8_t* il = (uint8_t*)malloc(size);
    fread(il, 1, size, f);
    fclose(f);
    
    cnd_program prog;
    if (cnd_program_load_il(&prog, il, size) != CND_ERR_OK) {
        printf("Failed to load IL!\n");
        return 1;
    }

    // 3. Prepare Data (Status OK)
    KitchenSink input_ok;
    memset(&input_ok, 0, sizeof(input_ok));
    input_ok.magic = 0xCAFEBABE;
    input_ok.flags_a = 1;
    input_ok.flag_b = true;
    input_ok.val_c = -10;
    input_ok.timestamp = 1678900000;
    input_ok.position.x = 1.5f; input_ok.position.y = -2.0f; input_ok.position.z = 3.14f;
    input_ok.matrix[0] = 10; input_ok.matrix[1] = 20; input_ok.matrix[2] = 30; input_ok.matrix[3] = 40;
    input_ok.points_len = 3;
    input_ok.points[0] = 100; input_ok.points[1] = 200; input_ok.points[2] = 300;
    strcpy(input_ok.name, "Kitchen Sink Demo");
    input_ok.status = STATUS_OK;
    input_ok.confidence = 99;
    input_ok.error_code = 0;
    input_ok.fail_reason[0] = 0;
    input_ok.percentage = 85;
    input_ok.temperature = 25.5; // stored as 255
    input_ok.has_extra = true;
    strcpy(input_ok.extra_data, "Extra!");
    input_ok.checksum = 0; // Recalculated by encoding
    input_ok.adv_mode = 0;
    input_ok.adv_simple_val = 12345;
    input_ok.adv_has_details = false;
    input_ok.adv_details[0] = 0;
    input_ok.adv_fallback_code = 0;

    // Advanced Switch: SIMPLE mode
    input_ok.adv_mode = 0;
    input_ok.adv_simple_val = 12345;
    input_ok.adv_has_details = false;
    input_ok.adv_details[0] = 0;
    input_ok.adv_fallback_code = 0;

    uint8_t buffer[1024];
    memset(buffer, 0, sizeof(buffer));
    cnd_vm_ctx ctx;

    // 4. Encode (OK)
    printf("\nEncoding 'OK' Packet...\n");
    cnd_init(&ctx, CND_MODE_ENCODE, &prog, buffer, sizeof(buffer), sink_cb, &input_ok);
    err = cnd_execute(&ctx);
    if (err != CND_ERR_OK) { printf("Encode Error: %d\n", err); return 1; }
    printf("Encoded %zu bytes.\n", ctx.cursor);
    
    // 5. Decode (OK)
    printf("Decoding...\n");
    KitchenSink output_ok;
    memset(&output_ok, 0, sizeof(output_ok));
    cnd_init(&ctx, CND_MODE_DECODE, &prog, buffer, ctx.cursor, sink_cb, &output_ok);
    err = cnd_execute(&ctx);
    if (err != CND_ERR_OK) { printf("Decode Error: %d\n", err); return 1; }
    
    printf("Decoded Data:\n");
    printf("  Magic: 0x%X\n", output_ok.magic);
    printf("  Flags: A=%d, B=%d, C=%d\n", output_ok.flags_a, output_ok.flag_b, output_ok.val_c);
    printf("  Pos: %.2f, %.2f, %.2f\n", output_ok.position.x, output_ok.position.y, output_ok.position.z);
    printf("  Status: %s (Confidence: %d%%)\n", output_ok.status == STATUS_OK ? "OK" : "FAIL", output_ok.confidence);
    printf("  Temp: %.1f\n", output_ok.temperature);
    printf("  Extra: %s\n", output_ok.has_extra ? output_ok.extra_data : "None");
    printf("  Checksum: 0x%X\n", output_ok.checksum);

    printf("  Advanced Switch: mode=%d\n", output_ok.adv_mode);
    if (output_ok.adv_mode == 0) {
        printf("    SIMPLE: adv_simple_val=%u\n", output_ok.adv_simple_val);
    } else if (output_ok.adv_mode == 1) {
        printf("    COMPLEX: adv_has_details=%d\n", output_ok.adv_has_details);
        if (output_ok.adv_has_details) {
            printf("      Details: %s\n", output_ok.adv_details);
        } else {
            printf("      Fallback Code: %u\n", output_ok.adv_fallback_code);
        }
    }

    // 6. Prepare Data (Status FAIL)
    printf("\nEncoding 'FAIL' Packet...\n");
    KitchenSink input_fail = input_ok;
    input_fail.status = STATUS_FAIL;
    input_fail.error_code = 500;
    strcpy(input_fail.fail_reason, "Internal Server Error");

    // Advanced Switch: COMPLEX mode with details
    KitchenSink input_adv_complex = input_ok;
    input_adv_complex.adv_mode = 1;
    input_adv_complex.adv_has_details = true;
    strcpy(input_adv_complex.adv_details, "Advanced details here!");
    input_adv_complex.adv_fallback_code = 0;

    // Advanced Switch: COMPLEX mode with fallback
    KitchenSink input_adv_fallback = input_ok;
    input_adv_fallback.adv_mode = 1;
    input_adv_fallback.adv_has_details = false;
    input_adv_fallback.adv_details[0] = 0;
    input_adv_fallback.adv_fallback_code = 77;
    
    // 7. Encode (FAIL)
    memset(buffer, 0, sizeof(buffer));
    cnd_init(&ctx, CND_MODE_ENCODE, &prog, buffer, sizeof(buffer), sink_cb, &input_fail);
    err = cnd_execute(&ctx);
    if (err != CND_ERR_OK) { printf("Encode Error: %d\n", err); return 1; }
    printf("Encoded %zu bytes.\n", ctx.cursor);

    // 8. Decode (FAIL)
    printf("Decoding...\n");
    KitchenSink output_fail;
    memset(&output_fail, 0, sizeof(output_fail));
    cnd_init(&ctx, CND_MODE_DECODE, &prog, buffer, ctx.cursor, sink_cb, &output_fail);
    err = cnd_execute(&ctx);
    if (err != CND_ERR_OK) { printf("Decode Error: %d\n", err); return 1; }
    
    printf("Decoded Data:\n");
    printf("  Status: %s\n", output_fail.status == STATUS_OK ? "OK" : "FAIL");
    printf("  Error Code: %d\n", output_fail.error_code);
    printf("  Reason: %s\n", output_fail.fail_reason);

    // Encode/Decode advanced complex
    printf("\nEncoding 'ADVANCED COMPLEX' Packet...\n");
    memset(buffer, 0, sizeof(buffer));
    cnd_init(&ctx, CND_MODE_ENCODE, &prog, buffer, sizeof(buffer), sink_cb, &input_adv_complex);
    err = cnd_execute(&ctx);
    if (err != CND_ERR_OK) { printf("Encode Error: %d\n", err); return 1; }
    printf("Encoded %zu bytes.\n", ctx.cursor);
    printf("Decoding...\n");
    KitchenSink output_adv_complex;
    memset(&output_adv_complex, 0, sizeof(output_adv_complex));
    cnd_init(&ctx, CND_MODE_DECODE, &prog, buffer, ctx.cursor, sink_cb, &output_adv_complex);
    err = cnd_execute(&ctx);
    if (err != CND_ERR_OK) { printf("Decode Error: %d\n", err); return 1; }
    printf("Decoded Data:\n");
    printf("  Advanced Switch: mode=%d\n", output_adv_complex.adv_mode);
    if (output_adv_complex.adv_mode == 1) {
        printf("    COMPLEX: adv_has_details=%d\n", output_adv_complex.adv_has_details);
        if (output_adv_complex.adv_has_details) {
            printf("      Details: %s\n", output_adv_complex.adv_details);
        } else {
            printf("      Fallback Code: %u\n", output_adv_complex.adv_fallback_code);
        }
    }

    // Encode/Decode advanced fallback
    printf("\nEncoding 'ADVANCED FALLBACK' Packet...\n");
    memset(buffer, 0, sizeof(buffer));
    cnd_init(&ctx, CND_MODE_ENCODE, &prog, buffer, sizeof(buffer), sink_cb, &input_adv_fallback);
    err = cnd_execute(&ctx);
    if (err != CND_ERR_OK) { printf("Encode Error: %d\n", err); return 1; }
    printf("Encoded %zu bytes.\n", ctx.cursor);
    printf("Decoding...\n");
    KitchenSink output_adv_fallback;
    memset(&output_adv_fallback, 0, sizeof(output_adv_fallback));
    cnd_init(&ctx, CND_MODE_DECODE, &prog, buffer, ctx.cursor, sink_cb, &output_adv_fallback);
    err = cnd_execute(&ctx);
    if (err != CND_ERR_OK) { printf("Decode Error: %d\n", err); return 1; }
    printf("Decoded Data:\n");
    printf("  Advanced Switch: mode=%d\n", output_adv_fallback.adv_mode);
    if (output_adv_fallback.adv_mode == 1) {
        printf("    COMPLEX: adv_has_details=%d\n", output_adv_fallback.adv_has_details);
        if (output_adv_fallback.adv_has_details) {
            printf("      Details: %s\n", output_adv_fallback.adv_details);
        } else {
            printf("      Fallback Code: %u\n", output_adv_fallback.adv_fallback_code);
        }
    }

    free(il);
    return 0;
}
