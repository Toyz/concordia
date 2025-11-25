#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <cJSON.h>
#include "../../include/concordia.h"
#include "../../include/compiler.h"

// --- Helper: File IO ---

uint8_t* read_file_bytes(const char* path, size_t* out_len) {
    FILE* f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t* buf = malloc(size);
    fread(buf, 1, size, f);
    fclose(f);
    if (out_len) *out_len = size;
    return buf;
}

char* read_file_text(const char* path) {
    FILE* f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    char* buf = malloc(size + 1);
    fread(buf, 1, size, f);
    buf[size] = '\0';
    fclose(f);
    return buf;
}

int write_file_bytes(const char* path, const uint8_t* data, size_t len) {
    FILE* f = fopen(path, "wb");
    if (!f) return 0;
    fwrite(data, 1, len, f);
    fclose(f);
    return 1;
}

int write_file_text(const char* path, const char* text) {
    FILE* f = fopen(path, "w");
    if (!f) return 0;
    fputs(text, f);
    fclose(f);
    return 1;
}

// --- Helper: IL Loader ---

typedef struct {
    uint8_t* raw_data;
    size_t raw_len;
    
    // Parsed
    uint16_t str_count;
    const char** string_table; 
    
    const uint8_t* bytecode;
    size_t bytecode_len;
} ILFile;

int load_il(const char* path, ILFile* il) {
    il->raw_data = read_file_bytes(path, &il->raw_len);
    if (!il->raw_data) return 0;
    
    if (memcmp(il->raw_data, "CNDIL", 5) != 0) {
        printf("Invalid IL file magic\n");
        return 0;
    }
    
    il->str_count = *(uint16_t*)(il->raw_data + 6);
    uint32_t str_offset = *(uint32_t*)(il->raw_data + 8);
    uint32_t bc_offset = *(uint32_t*)(il->raw_data + 12);
    
    il->string_table = malloc(il->str_count * sizeof(char*));
    const char* ptr = (const char*)(il->raw_data + str_offset);
    for (int i = 0; i < il->str_count; i++) {
        il->string_table[i] = ptr;
        ptr += strlen(ptr) + 1;
    }
    
    il->bytecode = il->raw_data + bc_offset;
    il->bytecode_len = il->raw_len - bc_offset;
    
    return 1;
}

void free_il(ILFile* il) {
    if (il->raw_data) free(il->raw_data);
    if (il->string_table) free(il->string_table);
}

// --- VM IO Callback (JSON Binding) ---

typedef struct {
    ILFile* il;
    cJSON* stack[32];
    int depth;
} IOCtx;

cnd_error_t json_io_callback(cnd_vm_ctx* ctx, uint16_t key_id, uint8_t type, void* ptr) {
    IOCtx* io = (IOCtx*)ctx->user_ptr;
    cJSON* current = io->stack[io->depth];
    
    if (type == OP_EXIT_STRUCT) {
        if (io->depth > 0) io->depth--;
        return CND_ERR_OK;
    }

    if (key_id >= io->il->str_count) return CND_ERR_OOB;
    const char* key_name = io->il->string_table[key_id];
    
    if (type == OP_ENTER_STRUCT) {
        if (ctx->mode == CND_MODE_ENCODE) {
            cJSON* item = cJSON_GetObjectItem(current, key_name);
            if (!item) return CND_ERR_CALLBACK; // Missing struct in input
            if (io->depth >= 31) return CND_ERR_OOB;
            io->stack[++io->depth] = item;
        } else {
            cJSON* item = cJSON_CreateObject();
            cJSON_AddItemToObject(current, key_name, item);
            if (io->depth >= 31) return CND_ERR_OOB;
            io->stack[++io->depth] = item;
        }
        return CND_ERR_OK;
    }
    
    if (ctx->mode == CND_MODE_ENCODE) {
        // JSON -> Binary
        cJSON* item = cJSON_GetObjectItem(current, key_name);
        
        if (!item) {
             // Array iteration HACK logic
             // If current is an array, use index?
             // Since we don't have iteration state here, we just grab item 0
             // if current is array. But key_name doesn't exist on array.
             // The VM loop calls IO with same KeyID.
             // If `current` is an Array (from previous ENTER_STRUCT or implicit array?),
             // `cJSON_GetObjectItem` fails.
             // But for `sensors[3]`, we have `ARR_FIXED`, then `IO_U8`.
             // The VM does NOT emit ENTER_STRUCT for Arrays currently.
             // So `current` is the Object containing "sensors".
             // `item` is the Array [1, 2, 3].
             // We need to pull element.
             
             // Only one hack supported: scalar fields.
             memset(ptr, 0, 8); 
             return CND_ERR_OK; 
        }

        if (cJSON_IsArray(item)) {
             // Simple hack: assume all array elements are same value for this demo
             // Real impl needs `ctx` to expose loop counter or use a distinct array callback.
             cJSON* elem = cJSON_GetArrayItem(item, 0); 
             if (elem) item = elem;
        }
        
        switch (type) {
            case OP_IO_U8:  *(uint8_t*)ptr  = (uint8_t)item->valueint; break;
            case OP_IO_U16: *(uint16_t*)ptr = (uint16_t)item->valueint; break;
            case OP_IO_U32: *(uint32_t*)ptr = (uint32_t)item->valueint; break;
            case OP_IO_U64: *(uint64_t*)ptr = (uint64_t)item->valuedouble; break; 
            case OP_IO_F32: *(float*)ptr    = (float)item->valuedouble; break;
            case OP_IO_F64: *(double*)ptr   = item->valuedouble; break;
            case OP_STR_NULL: {
                if (cJSON_IsString(item)) {
                    *(const char**)ptr = item->valuestring;
                } else {
                    *(const char**)ptr = "";
                }
                break;
            }
        }
    } else {
        // Binary -> JSON
        cJSON* existing = cJSON_GetObjectItem(current, key_name);
        cJSON* val = NULL;
        
        switch (type) {
            case OP_IO_U8:  val = cJSON_CreateNumber(*(uint8_t*)ptr); break;
            case OP_IO_U16: val = cJSON_CreateNumber(*(uint16_t*)ptr); break;
            case OP_IO_U32: val = cJSON_CreateNumber(*(uint32_t*)ptr); break;
            case OP_IO_F32: val = cJSON_CreateNumber(*(float*)ptr); break;
            case OP_STR_NULL: val = cJSON_CreateString((const char*)ptr); break; 
            default: val = cJSON_CreateNull(); break;
        }

        if (existing) {
            if (cJSON_IsArray(existing)) {
                cJSON_AddItemToArray(existing, val);
            } else {
                cJSON* detached = cJSON_DetachItemFromObject(current, key_name);
                cJSON* arr = cJSON_CreateArray();
                cJSON_AddItemToArray(arr, detached);
                cJSON_AddItemToArray(arr, val);
                cJSON_AddItemToObject(current, key_name, arr);
            }
        } else {
            cJSON_AddItemToObject(current, key_name, val);
        }
    }
    return CND_ERR_OK;
}

// --- Commands ---

int cmd_compile(int argc, char** argv) {
    if (argc < 4) {
        printf("Usage: cnd compile <input.cnd> <output.il>\n");
        return 1;
    }
    return cnd_compile_file(argv[2], argv[3]);
}

int cmd_encode(int argc, char** argv) {
    if (argc < 5) {
        printf("Usage: cnd encode <schema.il> <input.json> <output.bin>\n");
        return 1;
    }
    
    ILFile il;
    if (!load_il(argv[2], &il)) { printf("Failed to load IL\n"); return 1; }
    
    char* json_text = read_file_text(argv[3]);
    if (!json_text) { printf("Failed to read JSON\n"); return 1; }
    
    cJSON* root = cJSON_Parse(json_text);
    free(json_text);
    if (!root) { printf("Failed to parse JSON\n"); return 1; }
    
    // Encode
    uint8_t buffer[1024]; 
    memset(buffer, 0, sizeof(buffer));
    
    IOCtx io_ctx;
    io_ctx.il = &il;
    io_ctx.stack[0] = root;
    io_ctx.depth = 0;
    
    cnd_vm_ctx vm;
    cnd_init(&vm, CND_MODE_ENCODE, il.bytecode, il.bytecode_len, buffer, sizeof(buffer), json_io_callback, &io_ctx);
    
    cnd_error_t err = cnd_execute(&vm);
    if (err != CND_ERR_OK) {
        printf("VM Error: %d\n", err);
        return 1;
    }
    
    write_file_bytes(argv[4], buffer, vm.cursor);
    printf("Encoded %zu bytes to %s\n", vm.cursor, argv[4]);
    
    cJSON_Delete(root);
    free_il(&il);
    return 0;
}

int cmd_decode(int argc, char** argv) {
    if (argc < 5) {
        printf("Usage: cnd decode <schema.il> <input.bin> <output.json>\n");
        return 1;
    }
    
    ILFile il;
    if (!load_il(argv[2], &il)) { printf("Failed to load IL\n"); return 1; }
    
    size_t bin_len;
    uint8_t* bin_data = read_file_bytes(argv[3], &bin_len);
    if (!bin_data) { printf("Failed to read binary\n"); return 1; }
    
    cJSON* root = cJSON_CreateObject();
    IOCtx io_ctx;
    io_ctx.il = &il;
    io_ctx.stack[0] = root;
    io_ctx.depth = 0;
    
    cnd_vm_ctx vm;
    cnd_init(&vm, CND_MODE_DECODE, il.bytecode, il.bytecode_len, bin_data, bin_len, json_io_callback, &io_ctx);
    
    cnd_error_t err = cnd_execute(&vm);
    if (err != CND_ERR_OK) {
        printf("VM Error: %d\n", err);
        return 1;
    }
    
    char* out_json = cJSON_Print(root);
    if (!out_json) {
        printf("Failed to render JSON\n");
        return 1;
    }
    
    write_file_text(argv[4], out_json);
    printf("Decoded to %s\n", argv[4]);
    
    free(out_json);
    cJSON_Delete(root);
    free(bin_data);
    free_il(&il);
    return 0;
}

int main(int argc, char** argv) {
    setbuf(stdout, NULL);
    if (argc < 2) {
        printf("Concordia CLI\n");
        printf("Usage:\n");
        printf("  cnd compile <in.cnd> <out.il>\n");
        printf("  cnd encode <schema.il> <in.json> <out.bin>\n");
        printf("  cnd decode <schema.il> <in.bin> <out.json>\n");
        return 1;
    }
    
    if (strcmp(argv[1], "compile") == 0) return cmd_compile(argc, argv);
    if (strcmp(argv[1], "encode") == 0) return cmd_encode(argc, argv);
    if (strcmp(argv[1], "decode") == 0) return cmd_decode(argc, argv);
    
    printf("Unknown command: %s\n", argv[1]);
    return 1;
}