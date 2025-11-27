#include "../../include/cli_helpers.h"

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
    
    static IOCtx io_ctx; // Made static to persist
    io_ctx.il = &il;
    io_ctx.stack[0] = root;
    io_ctx.depth = 0;
    io_ctx.array_depth = 0; // Initialize array depth
    
    cnd_vm_ctx vm;
    cnd_program program;
    cnd_program_load(&program, il.bytecode, il.bytecode_len);
    cnd_init(&vm, CND_MODE_ENCODE, &program, buffer, sizeof(buffer), json_io_callback, &io_ctx);
    
    cnd_error_t err = cnd_execute(&vm);
    //fprintf(stderr, "DEBUG: cnd_execute returned %d\n", err);
    if (err != CND_ERR_OK) {
        fprintf(stderr, "VM Error: %d\n", err);
        return 1;
    }
    
    // fprintf(stderr, "DEBUG: Writing file...\n");
    size_t final_len = vm.cursor;
    if (vm.bit_offset > 0) final_len++;
    write_file_bytes(argv[4], buffer, final_len);
    // fprintf(stderr, "Encoded %zu bytes to %s\n", final_len, argv[4]);
    
    // fprintf(stderr, "DEBUG: Cleaning up...\n");
    cJSON_Delete(root);
    free_il(&il);
    // fprintf(stderr, "DEBUG: Done.\n");
    return 0;
}
