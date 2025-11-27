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
    lexer_init(&p.lexer, source);
    buf_init(&p.global_bc);
    strtab_init(&p.strtab);
    strtab_init(&p.imports);
    reg_init(&p.registry);
    enum_reg_init(&p.enums);
    p.had_error = 0;
    p.error_count = 0;
    p.target = &p.global_bc;
    p.current_path = in_path;
    p.json_output = json_output;
    p.current_struct_name = NULL;
    p.current_struct_name_len = 0;
    p.packet_count = 0;
    
    // Add main file to imports to prevent self-import
    strtab_add(&p.imports, in_path, (int)strlen(in_path));
    
    advance(&p); 
    parse_top_level(&p);
    
    if (p.had_error) { 
        if (!json_output) {
            printf("\nCompilation failed with %d error%s.\n", p.error_count, p.error_count == 1 ? "" : "s");
        }
        free(source); buf_free(&p.global_bc); return 1; 
    }

    FILE* out = fopen(out_path, "wb");
    if (!out) { 
        if (json_output) printf("{\"error\": \"Error opening output file: %s\"}\n", out_path);
        else printf("Error opening output file: %s\n", out_path); 
        return 1; 
    }

    fwrite("CNDIL", 1, 5, out); fputc(1, out);
    uint16_t str_count = (uint16_t)p.strtab.count; fwrite(&str_count, 2, 1, out); 
    
    uint32_t str_offset = 16;
    uint32_t str_bytes = 0;
    for(size_t i=0; i<p.strtab.count; i++) { str_bytes += strlen(p.strtab.strings[i]) + 1; }
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

    free(source); buf_free(&p.global_bc);
    return 0;
}
