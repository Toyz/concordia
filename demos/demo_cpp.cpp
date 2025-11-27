#include <iostream>
#include <fstream>
#include <vector>
#include <iomanip>
#include <cstring>
#include "concordia.h"

// --- Application Data Class ---
struct TelemetryData {
    uint32_t device_id;
    float temperature;
    uint8_t battery_level;
    uint8_t status;
};

// --- C++ Wrapper for Callback ---
// We can use a static method or a lambda-friendly wrapper.
// Since the VM is C, we must pass a C-compatible function pointer.
extern "C" cnd_error_t cpp_io_callback(cnd_vm_ctx* ctx, uint16_t key_id, uint8_t type, void* ptr) {
    auto* data = static_cast<TelemetryData*>(ctx->user_ptr);

    switch (key_id) {
        case 0: // device_id
            if (ctx->mode == CND_MODE_ENCODE) *static_cast<uint32_t*>(ptr) = data->device_id;
            else data->device_id = *static_cast<uint32_t*>(ptr);
            break;
        case 1: // temperature
            if (ctx->mode == CND_MODE_ENCODE) *static_cast<float*>(ptr) = data->temperature;
            else data->temperature = *static_cast<float*>(ptr);
            break;
        case 2: // battery_level
            if (ctx->mode == CND_MODE_ENCODE) *static_cast<uint8_t*>(ptr) = data->battery_level;
            else data->battery_level = *static_cast<uint8_t*>(ptr);
            break;
        case 3: // status
            if (ctx->mode == CND_MODE_ENCODE) *static_cast<uint8_t*>(ptr) = data->status;
            else data->status = *static_cast<uint8_t*>(ptr);
            break;
        default:
            return CND_ERR_INVALID_OP;
    }
    return CND_ERR_OK;
}

int main(int argc, char** argv) {
    if (argc < 2) {
        std::cerr << "Usage: demo_cpp <path_to_telemetry.il>" << std::endl;
        return 1;
    }

    // Load IL File
    std::ifstream file(argv[1], std::ios::binary | std::ios::ate);
    if (!file) {
        std::cerr << "Failed to open IL file" << std::endl;
        return 1;
    }
    std::streamsize size = file.tellg();
    file.seekg(0, std::ios::beg);
    std::vector<uint8_t> il_data(size);
    if (!file.read((char*)il_data.data(), size)) return 1;

    // Parse Header
    if (size < 16) return 1;
    uint32_t bytecode_offset = *reinterpret_cast<uint32_t*>(il_data.data() + 12);
    
    cnd_program program;
    cnd_program_load(&program, il_data.data() + bytecode_offset, size - bytecode_offset);

    // --- ENCODE ---
    std::cout << "--- C++ Encoding ---" << std::endl;
    TelemetryData data = { 0xCAFEBABE, 36.6f, 100, 0 };
    
    std::vector<uint8_t> buffer(128);
    cnd_vm_ctx ctx;
    cnd_init(&ctx, CND_MODE_ENCODE, &program, buffer.data(), buffer.size(), cpp_io_callback, &data);
    
    if (cnd_execute(&ctx) != CND_ERR_OK) {
        std::cerr << "Encoding failed" << std::endl;
        return 1;
    }

    std::cout << "Encoded " << ctx.cursor << " bytes: ";
    for(size_t i=0; i<ctx.cursor; i++) {
        std::cout << std::hex << std::setw(2) << std::setfill('0') << (int)buffer[i] << " ";
    }
    std::cout << std::dec << std::endl;

    // --- DECODE ---
    std::cout << "\n--- C++ Decoding ---" << std::endl;
    TelemetryData decoded = {0};
    cnd_init(&ctx, CND_MODE_DECODE, &program, buffer.data(), ctx.cursor, cpp_io_callback, &decoded);
    
    if (cnd_execute(&ctx) != CND_ERR_OK) {
        std::cerr << "Decoding failed" << std::endl;
        return 1;
    }

    std::cout << "Device ID: 0x" << std::hex << decoded.device_id << std::dec << std::endl;
    std::cout << "Temperature: " << decoded.temperature << std::endl;
    std::cout << "Battery: " << (int)decoded.battery_level << "%" << std::endl;

    return 0;
}
