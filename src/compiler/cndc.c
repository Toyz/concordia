#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "cnd_internal.h"

// Implementation of cnd_compile_file using the new modular structure
int cnd_compile_file(const char* in_path, const char* out_path, int json_output) {
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
    lexer_init(&p.lexer, source);
    buf_init(&p.global_bc);
    strtab_init(&p.strtab);
    strtab_init(&p.imports);
    reg_init(&p.registry);
    enum_reg_init(&p.enums);
    p.target = &p.global_bc;
    p.current_path = in_path;
    p.json_output = json_output;
    
    // Add main file to imports to prevent self-import
    strtab_add(&p.imports, in_path, (int)strlen(in_path));
    
    advance(&p);
    parse_top_level(&p);
    
    int ret = 0;

    if (p.had_error) {
        ret = 1;
    } else {
        FILE* out = fopen(out_path, "wb");
        if (!out) { 
            if (json_output) printf("{\"error\": \"Error opening output file: %s\"}\n", out_path);
            else printf("Error opening output file: %s\n", out_path); 
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
            if (json_output == 0) {
                printf("Successfully compiled %s to %s\n", in_path, out_path);
                printf("  Strings: %zu\n", p.strtab.count);
                printf("  Bytecode: %zu bytes\n", p.global_bc.size);
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
        for(size_t i=0; i<p.error_count && i<p.error_cap; i++) {
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
