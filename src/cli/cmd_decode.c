#include "cli_helpers.h"

int cmd_decode(int argc, char** argv) {
    if (argc < 5) {
        printf("Usage: cnd decode <schema.il> <input.bin> <output.json> [--hex]\n");
        return 1;
    }
    
    int hex_mode = 0;
    if (argc >= 6 && strcmp(argv[5], "--hex") == 0) {
        hex_mode = 1;
    }
    
    ILFile il;
    if (!load_il(argv[2], &il)) { printf("Failed to load IL\n"); return 1; }
    
    size_t bin_len;
    uint8_t* bin_data = read_file_bytes(argv[3], &bin_len);
    if (!bin_data) { printf("Failed to read binary\n"); return 1; }
    
    cJSON* root = cJSON_CreateObject();
    static IOCtx io_ctx; // Made static to persist
    memset(&io_ctx, 0, sizeof(IOCtx)); // Initialize all fields to 0
    io_ctx.il = &il;
    io_ctx.stack[0] = root;
    io_ctx.depth = 0;
    io_ctx.array_depth = 0; // Initialize array depth
    io_ctx.hex_mode = hex_mode;
    io_ctx.in_hex_byte_array = false; // Initialize new hex-related fields
    io_ctx.hex_str_buffer = NULL;
    io_ctx.hex_str_buffer_len = 0;
    io_ctx.hex_str_buffer_capacity = 0;
    
    cnd_vm_ctx vm;
    cnd_program program;
    cnd_program_load(&program, il.bytecode, il.bytecode_len);
    cnd_init(&vm, CND_MODE_DECODE, &program, bin_data, bin_len, json_io_callback, &io_ctx);
    
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
