#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "cnd_internal.h"

static int get_type_size(uint8_t type) {
    switch (type) {
        case OP_IO_U8: case OP_IO_I8: return 1;
        case OP_IO_U16: case OP_IO_I16: return 2;
        case OP_IO_U32: case OP_IO_I32: case OP_IO_F32: return 4;
        case OP_IO_U64: case OP_IO_I64: case OP_IO_F64: return 8;
        case OP_IO_BOOL: return 1;
        default: return 0;
    }
}

static void optimize_strings(Parser* p) {
    if (p->strtab.count == 0) return;

    // 1. Mark used strings
    uint8_t* used = calloc(p->strtab.count, 1);
    
    size_t offset = 0;
    uint8_t* bc = p->global_bc.data;
    size_t len = p->global_bc.size;

    while (offset < len) {
        uint8_t op = bc[offset++];
        
        // Opcodes with string ID at offset 0 (immediately after op)
        if (op == OP_META_NAME || 
            op == OP_ENTER_STRUCT ||
            (op >= OP_IO_U8 && op <= OP_IO_BOOL) ||
            (op >= OP_IO_BIT_U && op <= OP_IO_BIT_BOOL) ||
            (op >= OP_STR_NULL && op <= OP_STR_PRE_U32) ||
            (op >= OP_ARR_FIXED && op <= OP_ARR_PRE_U32) ||
            op == OP_CONST_CHECK ||
            op == OP_SWITCH ||
            op == OP_LOAD_CTX ||
            op == OP_CTX_QUERY ||
            op == OP_STORE_CTX) {
            
            if (offset + 2 > len) break;
            uint16_t id = *(uint16_t*)(bc + offset);
            if (id < p->strtab.count) used[id] = 1;
            offset += 2;
        }

        // Skip other arguments
        switch (op) {
            case OP_META_VERSION: offset += 1; break;
            case OP_IO_BIT_U: case OP_IO_BIT_I: case OP_IO_BIT_BOOL: offset += 1; break;
            case OP_ALIGN_PAD: case OP_ALIGN_FILL: offset += 1; break;
            case OP_ARR_FIXED: offset += 4; break; // u32 count
            case OP_ARR_PRE_U8: case OP_ARR_PRE_U16: case OP_ARR_PRE_U32: break; // No extra args
            
            case OP_RAW_BYTES: {
                if (offset + 4 > len) break;
                uint32_t count = *(uint32_t*)(bc + offset);
                offset += 4; // u32 count
                offset += count; // payload
                break;
            }

            case OP_CONST_CHECK: {
                if (offset + 1 > len) break;
                uint8_t type = bc[offset++];
                offset += get_type_size(type);
                break;
            }
            case OP_CONST_WRITE: {
                if (offset + 1 > len) break;
                uint8_t type = bc[offset++];
                offset += get_type_size(type);
                break;
            }
            case OP_RANGE_CHECK: {
                if (offset + 1 > len) break;
                uint8_t type = bc[offset++];
                int sz = get_type_size(type);
                offset += sz * 2; // min, max
                break;
            }
            case OP_ENUM_CHECK: {
                if (offset + 3 > len) break;
                uint8_t type = bc[offset++];
                uint16_t count = *(uint16_t*)(bc + offset); offset += 2;
                offset += count * get_type_size(type);
                break;
            }
            case OP_CRC_16: offset += 7; break; // poly(2), init(2), xor(2), flags(1)
            case OP_CRC_32: offset += 13; break; // poly(4), init(4), xor(4), flags(1)
            case OP_SCALE_LIN: offset += 16; break; // double, double
            case OP_TRANS_ADD: case OP_TRANS_SUB: case OP_TRANS_MUL: case OP_TRANS_DIV: offset += 8; break; // double
            case OP_TRANS_POLY: {
                if (offset + 1 > len) break;
                uint8_t count = bc[offset++];
                offset += count * 8;
                break;
            }
            case OP_TRANS_SPLINE: {
                if (offset + 1 > len) break;
                uint8_t count = bc[offset++];
                offset += count * 16; // 2 doubles per point
                break;
            }
            
            case OP_JUMP_IF_NOT: case OP_JUMP: offset += 4; break;
            case OP_SWITCH: {
                if (offset + 4 > len) break;
                offset += 4; // Table Offset
                break;
            }
            case OP_PUSH_IMM: {
                if (offset + 1 > len) break;
                uint8_t type = bc[offset++];
                offset += get_type_size(type);
                break;
            }
            case OP_STR_NULL: offset += 2; break; // max_len
            case OP_STR_PRE_U8: case OP_STR_PRE_U16: case OP_STR_PRE_U32: break; // No extra args
        }
    }

    // 2. Build new string table and map
    uint16_t* map = malloc(p->strtab.count * sizeof(uint16_t));
    StringTable new_tab;
    strtab_init(&new_tab);

    for (size_t i = 0; i < p->strtab.count; i++) {
        if (used[i]) {
            uint16_t new_id = strtab_add(&new_tab, p->strtab.strings[i], (int)strlen(p->strtab.strings[i]));
            map[i] = new_id;
        } else {
            map[i] = 0; // Should not be used
        }
    }

    // 3. Update bytecode
    offset = 0;
    while (offset < len) {
        uint8_t op = bc[offset++];
        
        if (op == OP_META_NAME || 
            op == OP_ENTER_STRUCT ||
            (op >= OP_IO_U8 && op <= OP_IO_BOOL) ||
            (op >= OP_IO_BIT_U && op <= OP_IO_BIT_BOOL) ||
            (op >= OP_STR_NULL && op <= OP_STR_PRE_U32) ||
            (op >= OP_ARR_FIXED && op <= OP_ARR_PRE_U32) ||
            op == OP_CONST_CHECK ||
            op == OP_SWITCH ||
            op == OP_LOAD_CTX ||
            op == OP_CTX_QUERY ||
            op == OP_STORE_CTX) {
            
            if (offset + 2 > len) break;
            uint16_t* id_ptr = (uint16_t*)(bc + offset);
            uint16_t old_id = *id_ptr;
            if (old_id < p->strtab.count) {
                *id_ptr = map[old_id];
            }
            offset += 2;
        }

        // Skip other arguments (same as above)
        switch (op) {
            case OP_META_VERSION: offset += 1; break;
            case OP_IO_BIT_U: case OP_IO_BIT_I: case OP_IO_BIT_BOOL: offset += 1; break;
            case OP_ALIGN_PAD: case OP_ALIGN_FILL: offset += 1; break;
            case OP_ARR_FIXED: offset += 4; break; // u32 count
            case OP_ARR_PRE_U8: case OP_ARR_PRE_U16: case OP_ARR_PRE_U32: break; // No extra args
            case OP_RAW_BYTES: {
                if (offset + 4 > len) break;
                uint32_t count = *(uint32_t*)(bc + offset);
                offset += 4; // u32 count
                offset += count; // payload
                break;
            }
            case OP_CONST_CHECK: {
                if (offset + 1 > len) break;
                uint8_t type = bc[offset++];
                offset += get_type_size(type);
                break;
            }
            case OP_CONST_WRITE: {
                if (offset + 1 > len) break;
                uint8_t type = bc[offset++];
                offset += get_type_size(type);
                break;
            }
            case OP_RANGE_CHECK: {
                if (offset + 1 > len) break;
                uint8_t type = bc[offset++];
                int sz = get_type_size(type);
                offset += sz * 2;
                break;
            }
            case OP_ENUM_CHECK: {
                if (offset + 3 > len) break;
                uint8_t type = bc[offset++];
                uint16_t count = *(uint16_t*)(bc + offset); offset += 2;
                offset += count * get_type_size(type);
                break;
            }
            case OP_CRC_16: offset += 7; break;
            case OP_CRC_32: offset += 13; break;
            case OP_SCALE_LIN: offset += 16; break;
            case OP_TRANS_ADD: case OP_TRANS_SUB: case OP_TRANS_MUL: case OP_TRANS_DIV: offset += 8; break;
            case OP_JUMP_IF_NOT: case OP_JUMP: offset += 4; break;
            case OP_SWITCH: {
                if (offset + 4 > len) break;
                offset += 4; // Table Offset
                break;
            }
            case OP_PUSH_IMM: {
                if (offset + 1 > len) break;
                uint8_t type = bc[offset++];
                offset += get_type_size(type);
                break;
            }
            case OP_STR_NULL: offset += 2; break; // max_len
            case OP_STR_PRE_U8: case OP_STR_PRE_U16: case OP_STR_PRE_U32: break; // No extra args
        }
    }

    // Replace string table
    for(size_t i=0; i<p->strtab.count; i++) free(p->strtab.strings[i]);
    free(p->strtab.strings);
    p->strtab = new_tab;

    free(used);
    free(map);
}

// Implementation of cnd_compile_file using the new modular structure
int cnd_compile_file(const char* in_path, const char* out_path, int json_output, int verbose) {
    setbuf(stdout, NULL); // Ensure debug prints are flushed immediately
    FILE* f = fopen(in_path, "rb");
    if (!f) { 
        if (json_output) printf("{\"error\": \"Error opening input file: %s\"}\n", in_path);
        else printf("Error opening input file: %s\n", in_path); 
        return 1; 
    }
    fseek(f, 0, SEEK_END); long size = ftell(f); fseek(f, 0, SEEK_SET);
    char* source = malloc(size + 1); fread(source, 1, size, f); source[size] = '\0';
    fclose(f);

    Parser p;
    memset(&p, 0, sizeof(Parser)); // Zero init
    p.verbose = verbose;
    lexer_init(&p.lexer, source);
    buf_init(&p.global_bc);
    strtab_init(&p.strtab);
    strtab_init(&p.imports);
    reg_init(&p.registry);
    enum_reg_init(&p.enums);
    p.target = &p.global_bc;
    p.current_path = in_path;
    p.json_output = json_output;
    p.verbose = verbose;
    
    // Add main file to imports to prevent self-import
    strtab_add(&p.imports, in_path, (int)strlen(in_path));
    
    advance(&p);
    parse_top_level(&p);
    
    int ret = 0;

    if (p.had_error) {
        ret = 1;
    } else {
        optimize_strings(&p);

        FILE* out = fopen(out_path, "wb");
        if (!out) { 
            if (json_output) printf("{\"status\": \"error\", \"message\": \"Error opening output file: %s\"}\n", out_path);
            else printf(COLOR_BOLD COLOR_RED "[ERROR]" COLOR_RESET " Error opening output file: %s\n", out_path); 
            ret = 1; 
        } else {
            fwrite("CNDIL", 1, 5, out); fputc(1, out);
            uint16_t str_count = (uint16_t)p.strtab.count; fwrite(&str_count, 2, 1, out); 
            
            uint32_t str_offset = 16;
            uint32_t str_bytes = 0;
            for(size_t i=0; i<p.strtab.count; i++) { str_bytes += (uint32_t)(strlen(p.strtab.strings[i]) + 1); }
            uint32_t bytecode_offset = str_offset + str_bytes;
            
            fwrite(&str_offset, 4, 1, out); fwrite(&bytecode_offset, 4, 1, out);
            
            for(size_t i=0; i<p.strtab.count; i++) { fwrite(p.strtab.strings[i], 1, strlen(p.strtab.strings[i]) + 1, out); }
            
            fwrite(p.global_bc.data, 1, p.global_bc.size, out);
            
            fclose(out);
            if (json_output) {
                // Escape paths for JSON (simple check)
                // For now assuming paths don't have crazy characters, but in production should be escaped properly
                printf("{\"status\": \"success\", \"input\": \"%s\", \"output\": \"%s\", \"stats\": {\"strings\": %zu, \"bytecode_size\": %zu}}\n",
                    in_path, out_path, p.strtab.count, p.global_bc.size);
            } else {
                printf(COLOR_BOLD COLOR_GREEN "[SUCCESS]" COLOR_RESET " Compiled " COLOR_CYAN "%s" COLOR_RESET "\n", in_path);
                printf("  " COLOR_BOLD "Output:" COLOR_RESET "   %s\n", out_path);
                printf("  " COLOR_BOLD "Stats:" COLOR_RESET "    %zu strings, %zu bytes bytecode\n", p.strtab.count, p.global_bc.size);
            }
        }
    }

    // Cleanup
    free(source);
    buf_free(&p.global_bc);
    
    // Free registries
    if (p.registry.defs) {
        for(size_t i=0; i<p.registry.count; i++) {
            free(p.registry.defs[i].name);
            buf_free(&p.registry.defs[i].bytecode);
            if(p.registry.defs[i].file) free(p.registry.defs[i].file);
            if(p.registry.defs[i].doc_comment) free(p.registry.defs[i].doc_comment);
        }
        free(p.registry.defs);
    }
    if (p.enums.defs) {
        for(size_t i=0; i<p.enums.count; i++) {
            free(p.enums.defs[i].name);
            if(p.enums.defs[i].file) free(p.enums.defs[i].file);
            if(p.enums.defs[i].doc_comment) free(p.enums.defs[i].doc_comment);
            if(p.enums.defs[i].values) {
                for(size_t j=0; j<p.enums.defs[i].count; j++) {
                    free(p.enums.defs[i].values[j].name);
                    if(p.enums.defs[i].values[j].doc_comment) free(p.enums.defs[i].values[j].doc_comment);
                }
                free(p.enums.defs[i].values);
            }
        }
        free(p.enums.defs);
    }
    if (p.errors) {
        for(size_t i=0; i<(size_t)p.error_count && i<p.error_cap; i++) {
            if(p.errors[i].message) free(p.errors[i].message);
        }
        free(p.errors);
    }
    
    // Free string table contents
    if (p.strtab.strings) {
        for(size_t i=0; i<p.strtab.count; i++) free(p.strtab.strings[i]);
        free(p.strtab.strings);
    }
    // Free imports table contents
    if (p.imports.strings) {
        for(size_t i=0; i<p.imports.count; i++) free(p.imports.strings[i]);
        free(p.imports.strings);
    }
    
    return ret;
}
