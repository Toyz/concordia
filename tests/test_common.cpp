#include "test_common.h"
#include <cstring>
#include <cstdio>

test_data_entry g_test_data[MAX_TEST_ENTRIES];

void clear_test_data() {
    memset(g_test_data, 0, sizeof(g_test_data));
    for(int i=0; i<MAX_TEST_ENTRIES; i++) g_test_data[i].key = 0xFFFF;
}

// C-compatible callback for the VM
extern "C" cnd_error_t test_io_callback(cnd_vm_ctx* ctx, uint16_t key_id, uint8_t type, void* ptr) {
    // printf("Callback: Key %d, Type %d, Mode %d\n", key_id, type, ctx->mode);
    if (type == OP_ARR_END || type == OP_EXIT_STRUCT || type == OP_ENTER_STRUCT || type == OP_ARR_FIXED) return CND_ERR_OK;

    int idx = -1;
    
    TestContext* tctx = (TestContext*)ctx->user_ptr;
    if (tctx && tctx->use_tape) {
        if (tctx->tape_index >= MAX_TEST_ENTRIES) return CND_ERR_OOB;
        // Verify key matches expected
        if (g_test_data[tctx->tape_index].key != key_id && g_test_data[tctx->tape_index].key != 0xFFFF) {
                printf("Tape Mismatch! Expected Key %d, Got Key %d at Index %d\n", 
                    g_test_data[tctx->tape_index].key, key_id, tctx->tape_index);
                return CND_ERR_CALLBACK;
        }
        idx = tctx->tape_index;
        tctx->tape_index++;
    } else {
        // Legacy Search Mode
        for(int i=0; i<MAX_TEST_ENTRIES; i++) {
            if (g_test_data[i].key == key_id) {
                idx = i;
                break;
            }
        }
    }
    
    if (type == OP_CTX_QUERY || type == OP_LOAD_CTX) {
        if (idx != -1) *(uint64_t*)ptr = g_test_data[idx].u64_val;
        else return CND_ERR_CALLBACK;
        return CND_ERR_OK;
    }

    if (type == OP_STORE_CTX) {
        if (idx == -1) {
             for(int i=0; i<MAX_TEST_ENTRIES; i++) {
                if (g_test_data[i].key == 0xFFFF) { 
                     g_test_data[i].key = key_id;
                     idx = i;
                     break;
                }
             }
        }
        if (idx == -1) return CND_ERR_OOB;
        g_test_data[idx].u64_val = *(uint64_t*)ptr;
        return CND_ERR_OK;
    }

    if (ctx->mode == CND_MODE_ENCODE) {
        if (type == OP_ENTER_STRUCT || type == OP_EXIT_STRUCT) return CND_ERR_OK;
        if (idx == -1) return CND_ERR_CALLBACK;
        
        switch(type) {
            case OP_IO_U8:  *(uint8_t*)ptr = (uint8_t)g_test_data[idx].u64_val; break;
            case OP_IO_U16: *(uint16_t*)ptr = (uint16_t)g_test_data[idx].u64_val; break;
            case OP_IO_U32: *(uint32_t*)ptr = (uint32_t)g_test_data[idx].u64_val; break;
            case OP_IO_U64: *(uint64_t*)ptr = g_test_data[idx].u64_val; break;
            
            case OP_ARR_PRE_U8: *(uint8_t*)ptr = (uint8_t)g_test_data[idx].u64_val; break;
            case OP_ARR_PRE_U16: *(uint16_t*)ptr = (uint16_t)g_test_data[idx].u64_val; break;
            case OP_ARR_PRE_U32: *(uint32_t*)ptr = (uint32_t)g_test_data[idx].u64_val; break;

            case OP_IO_I8:  *(int8_t*)ptr = (int8_t)g_test_data[idx].u64_val; break;
            case OP_IO_I16: *(int16_t*)ptr = (int16_t)g_test_data[idx].u64_val; break;
            case OP_IO_I32: *(int32_t*)ptr = (int32_t)g_test_data[idx].u64_val; break;
            case OP_IO_I64: *(int64_t*)ptr = (int64_t)g_test_data[idx].u64_val; break;

            case OP_IO_BOOL: *(uint8_t*)ptr = (uint8_t)g_test_data[idx].u64_val; break;
            
            case OP_IO_F32: *(float*)ptr = (float)g_test_data[idx].f64_val; break;
            case OP_IO_F64: *(double*)ptr = g_test_data[idx].f64_val; break;

            case OP_IO_BIT_U: *(uint64_t*)ptr = g_test_data[idx].u64_val; break;
            case OP_IO_BIT_I: *(int64_t*)ptr = (int64_t)g_test_data[idx].u64_val; break;
            case OP_IO_BIT_BOOL: *(uint8_t*)ptr = (uint8_t)g_test_data[idx].u64_val; break;
            
            case OP_STR_NULL:
            case OP_STR_PRE_U8:
            case OP_STR_PRE_U16:
            case OP_STR_PRE_U32:
                *(const char**)ptr = g_test_data[idx].string_val;
                break;

            /*
            case OP_JUMP_IF_NOT:
                *(uint64_t*)ptr = g_test_data[idx].u64_val;
                break;
            */

            case OP_ARR_FIXED:
            case OP_ARR_END:
                return CND_ERR_OK;

            default: return CND_ERR_INVALID_OP;
        }
    } else {
        // DECODE MODE
        if (idx == -1) {
             for(int i=0; i<MAX_TEST_ENTRIES; i++) {
                if (g_test_data[i].key == 0xFFFF) { 
                     g_test_data[i].key = key_id;
                     idx = i;
                     break;
                }
            }
        }
        
        if (idx == -1) return CND_ERR_CALLBACK; // No space left

        // Always update key (useful for Tape Mode)
        g_test_data[idx].key = key_id;
        
        switch(type) {
            case OP_IO_U8:  g_test_data[idx].u64_val = *(uint8_t*)ptr; break;
            case OP_IO_U16: g_test_data[idx].u64_val = *(uint16_t*)ptr; break;
            case OP_IO_U32: g_test_data[idx].u64_val = *(uint32_t*)ptr; break;
            case OP_IO_U64: g_test_data[idx].u64_val = *(uint64_t*)ptr; break;
            
            case OP_IO_I8:  g_test_data[idx].u64_val = (uint64_t)*(int8_t*)ptr; break;
            case OP_IO_I16: g_test_data[idx].u64_val = (uint64_t)*(int16_t*)ptr; break;
            case OP_IO_I32: g_test_data[idx].u64_val = (uint64_t)*(int32_t*)ptr; break;
            case OP_IO_I64: g_test_data[idx].u64_val = (uint64_t)*(int64_t*)ptr; break;

            case OP_IO_BOOL: g_test_data[idx].u64_val = *(uint8_t*)ptr; break;

            case OP_IO_F32: g_test_data[idx].f64_val = *(float*)ptr; break;
            case OP_IO_F64: g_test_data[idx].f64_val = *(double*)ptr; break;

            case OP_IO_BIT_U: g_test_data[idx].u64_val = *(uint64_t*)ptr; break;
            case OP_IO_BIT_I: g_test_data[idx].u64_val = (uint64_t)*(int64_t*)ptr; break;
            case OP_IO_BIT_BOOL: g_test_data[idx].u64_val = *(uint8_t*)ptr; break;
            
            case OP_ARR_PRE_U8: g_test_data[idx].u64_val = *(uint8_t*)ptr; break;
            case OP_ARR_PRE_U16: g_test_data[idx].u64_val = *(uint16_t*)ptr; break;
            case OP_ARR_PRE_U32: g_test_data[idx].u64_val = *(uint32_t*)ptr; break;

            case OP_STR_NULL:
                #ifdef _MSC_VER
                strncpy_s(g_test_data[idx].string_val, sizeof(g_test_data[idx].string_val), (const char*)ptr, 63);
                #else
                strncpy(g_test_data[idx].string_val, (const char*)ptr, 63);
                #endif
                break;

            case OP_STR_PRE_U8: {
                uint8_t len = ((uint8_t*)ptr)[-1];
                size_t copy_len = len < 63 ? len : 63;
                memcpy(g_test_data[idx].string_val, ptr, copy_len);
                g_test_data[idx].string_val[copy_len] = 0;
                break;
            }

            case OP_STR_PRE_U16:
            case OP_STR_PRE_U32:
                // TODO: Handle length reading for U16/U32 (requires endianness check)
                // For now, fallback to strncpy but this is unsafe for non-null-terminated buffers
                #ifdef _MSC_VER
                strncpy_s(g_test_data[idx].string_val, sizeof(g_test_data[idx].string_val), (const char*)ptr, 63);
                #else
                strncpy(g_test_data[idx].string_val, (const char*)ptr, 63);
                #endif
                break;

            case OP_ARR_FIXED:
            case OP_ARR_END:
            case OP_ENTER_STRUCT:
            case OP_EXIT_STRUCT:
                return CND_ERR_OK;

            default: return CND_ERR_INVALID_OP;
        }
    }
    return CND_ERR_OK;
}
