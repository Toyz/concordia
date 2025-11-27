#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "concordia.h"

// --- Application Data Structure ---
typedef struct {
    uint32_t device_id;
    float temperature;
    uint8_t battery_level;
    uint8_t status;
} TelemetryData;

// --- IO Callback ---
// This function maps the VM's requests (by Key ID) to our C struct.
// In a real app, you might use a generated header to map names to IDs,
// or a hash map if you want dynamic lookup.
// Here, we know the order from the schema:
// 0: device_id
// 1: temperature
// 2: battery_level
// 3: status
cnd_error_t my_io_callback(cnd_vm_ctx* ctx, uint16_t key_id, uint8_t type, void* ptr) {
    TelemetryData* data = (TelemetryData*)ctx->user_ptr;

    // Debug print with Key Name lookup
    const char* key_name = cnd_get_key_name(ctx->program, key_id);
    if (key_name) {
        printf("IO Callback: Key '%s' (%d), Type %d\n", key_name, key_id, type);
    }

    switch (key_id) {
        case 0: // device_id (uint32)
            if (ctx->mode == CND_MODE_ENCODE) *(uint32_t*)ptr = data->device_id;
            else data->device_id = *(uint32_t*)ptr;
            break;
        case 1: // temperature (float)
            if (ctx->mode == CND_MODE_ENCODE) *(float*)ptr = data->temperature;
            else data->temperature = *(float*)ptr;
            break;
        case 2: // battery_level (uint8)
            if (ctx->mode == CND_MODE_ENCODE) *(uint8_t*)ptr = data->battery_level;
            else data->battery_level = *(uint8_t*)ptr;
            break;
        case 3: // status (uint8)
            if (ctx->mode == CND_MODE_ENCODE) *(uint8_t*)ptr = data->status;
            else data->status = *(uint8_t*)ptr;
            break;
        default:
            return CND_ERR_INVALID_OP; // Unknown field
    }
    return CND_ERR_OK;
}

// --- Helper to load IL file ---
uint8_t* load_file(const char* path, size_t* size) {
    FILE* f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    *size = ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t* buf = (uint8_t*)malloc(*size);
    fread(buf, 1, *size, f);
    fclose(f);
    return buf;
}

int main(int argc, char** argv) {
    if (argc < 2) {
        printf("Usage: demo_c <path_to_telemetry.il>\n");
        printf("Please compile 'telemetry.cnd' first using: cnd compile telemetry.cnd telemetry.il\n");
        return 1;
    }

    const char* il_path = argv[1];
    size_t file_size;
    uint8_t* file_data = load_file(il_path, &file_size);
    if (!file_data) {
        printf("Failed to open IL file: %s\n", il_path);
        return 1;
    }

    // Parse IL Header and Load Program
    cnd_program program;
    if (cnd_program_load_il(&program, file_data, file_size) != CND_ERR_OK) {
        printf("Invalid IL file format\n");
        return 1;
    }

    // --- ENCODE ---
    printf("--- Encoding ---\n");
    TelemetryData my_data = {
        .device_id = 0x12345678,
        .temperature = 25.5f,
        .battery_level = 85,
        .status = 1 // Connected
    };

    uint8_t buffer[128];
    memset(buffer, 0, sizeof(buffer));
    cnd_vm_ctx ctx;

    cnd_init(&ctx, CND_MODE_ENCODE, &program, buffer, sizeof(buffer), my_io_callback, &my_data);
    cnd_error_t err = cnd_execute(&ctx);
    
    if (err != CND_ERR_OK) {
        printf("Encoding failed with error %d\n", err);
        return 1;
    }

    printf("Encoded %zu bytes:\n", ctx.cursor);
    for(size_t i=0; i<ctx.cursor; i++) printf("%02X ", buffer[i]);
    printf("\n");

    // Save to file
    FILE* out = fopen("telemetry.bin", "wb");
    fwrite(buffer, 1, ctx.cursor, out);
    fclose(out);
    printf("Saved to telemetry.bin\n");

    // --- DECODE ---
    printf("\n--- Decoding ---\n");
    TelemetryData decoded_data = {0};
    
    // Reset context for decoding
    cnd_init(&ctx, CND_MODE_DECODE, &program, buffer, ctx.cursor, my_io_callback, &decoded_data);
    err = cnd_execute(&ctx);

    if (err != CND_ERR_OK) {
        printf("Decoding failed with error %d\n", err);
        return 1;
    }

    printf("Decoded Data:\n");
    printf("  Device ID: 0x%X\n", decoded_data.device_id);
    printf("  Temperature: %.1f C\n", decoded_data.temperature);
    printf("  Battery: %d%%\n", decoded_data.battery_level);
    printf("  Status: %d\n", decoded_data.status);

    free(file_data);
    return 0;
}
