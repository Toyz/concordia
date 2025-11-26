#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "../../include/cli_helpers.h"
#include <cJSON.h> // Include cJSON specifically here for its types
#include "../../include/concordia.h" // For CND_ERR_OOB etc.
#include "../../include/compiler.h" // For cnd_compile_file

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

cnd_error_t json_io_callback(cnd_vm_ctx* ctx, uint16_t key_id, uint8_t type, void* ptr) {
    // Re-evaluate current_obj_context dynamically
    IOCtx* io = (IOCtx*)ctx->user_ptr;
    cJSON* current_obj_context = io->stack[io->depth]; 
    
    const char* key_name = ""; // Initialize to avoid warning
    if (key_id < io->il->str_count) {
        key_name = io->il->string_table[key_id];
    }
    
    // printf("CALLBACK: Type=0x%x, Key='%s', Depth=%d, ArrayDepth=%d\n", type, key_name, io->depth, io->array_depth);

    if (type == OP_EXIT_STRUCT) {
        if (io->depth > 0) io->depth--;
        //printf("CALLBACK: Exited struct. Depth=%d\n", io->depth);
        return CND_ERR_OK;
    }

    // Array End
    if (type == OP_ARR_END) {
        if (io->array_depth > 0) io->array_depth--;
        //printf("CALLBACK: Exited array. Array Depth=%d\n", io->array_depth);
        return CND_ERR_OK;
    }


    if (key_id >= io->il->str_count) {
        // printf("CALLBACK ERROR: KeyID %d out of bounds for string table size %d\n", key_id, io->il->str_count);
        return CND_ERR_OOB;
    }
    
    if (type == OP_ENTER_STRUCT) {
        cJSON* item = NULL;

        // Check if we should consume an array element (Struct inside Array)
        if (io->array_depth > 0 && io->depth == io->array_start_depth[io->array_depth - 1]) {
             if (ctx->mode == CND_MODE_ENCODE) {
                 cJSON* current_arr = io->array_stack[io->array_depth - 1];
                 int current_idx = io->array_index_stack[io->array_depth - 1];
                 item = cJSON_GetArrayItem(current_arr, current_idx);
                 io->array_index_stack[io->array_depth - 1]++;
             } else { // DECODE
                 cJSON* current_arr = io->array_stack[io->array_depth - 1];
                 item = cJSON_CreateObject();
                 cJSON_AddItemToArray(current_arr, item);
                 io->array_index_stack[io->array_depth - 1]++;
             }
        }

        if (ctx->mode == CND_MODE_ENCODE) {
            if (!item) {
                item = cJSON_GetObjectItem(current_obj_context, key_name);
                if (!item) {
                    char* current_obj_str = cJSON_PrintUnformatted(current_obj_context);
                    printf("CALLBACK ERROR: ENCODE - Missing struct '%s' in JSON (for current_obj %s). Returning CND_ERR_CALLBACK.\n", key_name, current_obj_str);
                    free(current_obj_str);
                    return CND_ERR_CALLBACK; // Missing struct in input
                }
            }
            if (io->depth >= 31) return CND_ERR_OOB;
            io->stack[++io->depth] = item;
            // printf("CALLBACK: Entered struct '%s'. New Depth=%d. New current_obj (JSON): %s\n", key_name, io->depth, cJSON_PrintUnformatted(io->stack[io->depth]));
        } else { // DECODE
            if (!item) {
                item = cJSON_CreateObject();
                cJSON_AddItemToObject(current_obj_context, key_name, item);
            }
            if (io->depth >= 31) return CND_ERR_OOB;
            io->stack[++io->depth] = item;
            // printf("CALLBACK: Entered struct '%s'. New Depth=%d. New current_obj (JSON): %s\n", key_name, io->depth, cJSON_PrintUnformatted(io->stack[io->depth]));
        }
        return CND_ERR_OK;
    }
    
    // Array Prefixes (Mark array start)
    if (type == OP_ARR_PRE_U8 || type == OP_ARR_PRE_U16 || type == OP_ARR_PRE_U32 || type == OP_ARR_FIXED) {
        if (ctx->mode == CND_MODE_ENCODE) {
            cJSON* item = cJSON_GetObjectItem(current_obj_context, key_name);
            if (!item || !cJSON_IsArray(item)) {
                // If no array found, assume 0 length
                if (type == OP_ARR_PRE_U8) *(uint8_t*)ptr = 0;
                else if (type == OP_ARR_PRE_U16) *(uint16_t*)ptr = 0;
                else if (type == OP_ARR_PRE_U32) *(uint32_t*)ptr = 0;
                else if (type == OP_ARR_FIXED) *(uint32_t*)ptr = 0; // For FIXED arrays, this is redundant as count is in IL
                printf("CALLBACK WARNING: ENCODE - ARR_PRE/FIXED (Key '%s'): Item not array or missing in JSON. Returning 0 length.\n", key_name);
                return CND_ERR_OK;
            }
            if (io->array_depth >= 31) return CND_ERR_OOB;
            io->array_stack[io->array_depth] = item;
            io->array_index_stack[io->array_depth] = 0;
            io->array_start_depth[io->array_depth] = io->depth;
            io->array_depth++;

            int array_size = cJSON_GetArraySize(item);

            if (type == OP_ARR_PRE_U8) { 
                uint8_t* count_ptr = (uint8_t*)ptr;
                *count_ptr = (uint8_t)array_size;
            }
            else if (type == OP_ARR_PRE_U16) { 
                uint16_t* count_ptr = (uint16_t*)ptr;
                *count_ptr = (uint16_t)array_size;
            }
            else if (type == OP_ARR_PRE_U32) { 
                uint32_t* count_ptr = (uint32_t*)ptr;
                *count_ptr = (uint32_t)array_size;
            }
            // OP_ARR_FIXED: Do not write to ptr. The count is fixed by schema (IL), 
            // and we should not override it with the actual data size.
            
            return CND_ERR_OK;
        } else { // DECODE
            // For decode, the VM gives us the count. We just initialize array context.
            if (io->array_depth >= 31) return CND_ERR_OOB;
            cJSON* arr_item = cJSON_CreateArray();
            cJSON_AddItemToObject(current_obj_context, key_name, arr_item); // Add empty array to current object
            io->array_stack[io->array_depth] = arr_item;
            io->array_index_stack[io->array_depth] = 0;
            io->array_start_depth[io->array_depth] = io->depth;
            io->array_depth++;
            return CND_ERR_OK; // VM will continue loop, adding elements
        }
    }


    cJSON* item_to_process = NULL; // For primitives and strings
    // Determine which JSON item to use: either the current object, or an element from the current array
    if (io->array_depth > 0 && io->depth == io->array_start_depth[io->array_depth - 1]) { // If currently inside an array loop AND at array level
        cJSON* current_arr = io->array_stack[io->array_depth - 1];
        int current_idx = io->array_index_stack[io->array_depth - 1];

        if (ctx->mode == CND_MODE_ENCODE) {
            item_to_process = cJSON_GetArrayItem(current_arr, current_idx);
            io->array_index_stack[io->array_depth - 1]++; // Advance index
        } else { // DECODE
            // For decode, we just ensure the element gets created and added to current_arr
            // The switch below will create 'val'
            item_to_process = current_arr; // This will be the parent to add to
            io->array_index_stack[io->array_depth - 1]++; // Index already advanced in decode.
        }
    } else {
        // Not in array, use current object for item_to_process
        item_to_process = cJSON_GetObjectItem(current_obj_context, key_name); 
    }
    
    // Now process the primitive/string value
    if (ctx->mode == CND_MODE_ENCODE) {
        if (!item_to_process) {
            char* current_obj_str = cJSON_PrintUnformatted(current_obj_context);
            printf("CALLBACK ERROR: ENCODE - Primitive/String '%s' not found in JSON (current_obj %s, key_name '%s'). Returning CND_ERR_CALLBACK.\n", key_name, current_obj_str, key_name);
            free(current_obj_str);
            return CND_ERR_CALLBACK; 
        }

        if (cJSON_IsArray(item_to_process)) { // Should not happen here if array_depth handles it for elements
             // Fallback for non-array elements erroneously returned as array
             cJSON* elem = cJSON_GetArrayItem(item_to_process, 0); 
             if (elem) item_to_process = elem;
        }
        
        switch (type) {
            case OP_IO_U8:  *(uint8_t*)ptr  = (uint8_t)item_to_process->valueint; break;
            case OP_IO_U16: *(uint16_t*)ptr = (uint16_t)item_to_process->valueint; break;
            case OP_IO_U32: *(uint32_t*)ptr = (uint32_t)item_to_process->valueint; break;
            case OP_IO_U64: *(uint64_t*)ptr = (uint64_t)item_to_process->valuedouble; break; 
            case OP_IO_I8:  *(int8_t*)ptr   = (int8_t)item_to_process->valueint; break;
            case OP_IO_I16: *(int16_t*)ptr  = (int16_t)item_to_process->valueint; break;
            case OP_IO_I32: *(int32_t*)ptr  = (int32_t)item_to_process->valueint; break;
            case OP_IO_I64: *(int64_t*)ptr  = (int64_t)item_to_process->valuedouble; break;
            case OP_IO_F32: *(float*)ptr    = (float)item_to_process->valuedouble; break;
            case OP_IO_F64: *(double*)ptr   = (double)item_to_process->valuedouble; break;
            case OP_IO_BIT_U: *(uint64_t*)ptr = (uint64_t)item_to_process->valueint; break;
            case OP_IO_BIT_I: *(int64_t*)ptr  = (int64_t)item_to_process->valueint; break;
            case OP_STR_NULL: 
            case OP_STR_PRE_U8:
            case OP_STR_PRE_U16:
            case OP_STR_PRE_U32: {
                if (cJSON_IsString(item_to_process)) {
                    *(const char**)ptr = item_to_process->valuestring;
                } else {
                    *(const char**)ptr = "";
                }
                break;
            }
        }
    } else { // DECODE MODE
        cJSON* val = NULL;
        
        switch (type) {
            case OP_IO_U8:  val = cJSON_CreateNumber(*(uint8_t*)ptr); break;
            case OP_IO_U16: val = cJSON_CreateNumber(*(uint16_t*)ptr); break;
            case OP_IO_U32: val = cJSON_CreateNumber(*(uint32_t*)ptr); break;
            case OP_IO_I8:  val = cJSON_CreateNumber(*(int8_t*)ptr); break;
            case OP_IO_I16: val = cJSON_CreateNumber(*(int16_t*)ptr); break;
            case OP_IO_I32: val = cJSON_CreateNumber(*(int32_t*)ptr); break;
            case OP_IO_I64: val = cJSON_CreateNumber((double)*(int64_t*)ptr); break;
            case OP_IO_F32: val = cJSON_CreateNumber(*(float*)ptr); break;
            case OP_IO_BIT_U: val = cJSON_CreateNumber((double)*(uint64_t*)ptr); break;
            case OP_IO_BIT_I: val = cJSON_CreateNumber((double)*(int64_t*)ptr); break;
            case OP_STR_NULL: val = cJSON_CreateString((const char*)ptr); break; 
            
            case OP_STR_PRE_U8: {
                uint8_t len = ((uint8_t*)ptr)[-1];
                char* temp = malloc(len + 1);
                memcpy(temp, ptr, len);
                temp[len] = 0;
                val = cJSON_CreateString(temp);
                free(temp);
                break;
            }
            case OP_STR_PRE_U16: {
                uint16_t len = 0;
                uint8_t* raw = (uint8_t*)ptr - 2;
                if (ctx->endianness == CND_LE) {
                    len = raw[0] | (raw[1] << 8);
                } else {
                    len = (raw[0] << 8) | raw[1];
                }
                
                char* temp = malloc(len + 1);
                memcpy(temp, ptr, len);
                temp[len] = 0;
                val = cJSON_CreateString(temp);
                free(temp);
                break;
            }
            case OP_STR_PRE_U32: {
                uint32_t len = 0;
                uint8_t* raw = (uint8_t*)ptr - 4;
                if (ctx->endianness == CND_LE) {
                    len = raw[0] | (raw[1] << 8) | (raw[2] << 16) | (raw[3] << 24);
                } else {
                    len = (raw[0] << 24) | (raw[1] << 16) | (raw[2] << 8) | raw[3];
                }
                
                char* temp = malloc(len + 1);
                memcpy(temp, ptr, len);
                temp[len] = 0;
                val = cJSON_CreateString(temp);
                free(temp);
                break;
            }

            // Other Array/Struct ops are handled above
            default: val = cJSON_CreateNull(); break;
        }

        if (io->array_depth > 0 && io->depth == io->array_start_depth[io->array_depth - 1]) { // If inside an array, add to the current array
            cJSON* current_arr = io->array_stack[io->array_depth - 1];
            cJSON_AddItemToArray(current_arr, val);
            // Index already incremented above.
        } else { // Not in an array, add to current object
            cJSON_AddItemToObject(current_obj_context, key_name, val);
        }
    }
    return CND_ERR_OK;
}
