#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "concordia.h"

// Helper to read bytes from memory safely
static uint8_t read_u8(const uint8_t** ptr, const uint8_t* end) {
    if (*ptr >= end) return 0;
    return *(*ptr)++;
}

static uint16_t read_u16(const uint8_t** ptr, const uint8_t* end) {
    if (*ptr + 2 > end) return 0;
    uint16_t v = (*ptr)[0] | ((*ptr)[1] << 8);
    *ptr += 2;
    return v;
}

static uint32_t read_u32(const uint8_t** ptr, const uint8_t* end) {
    if (*ptr + 4 > end) return 0;
    uint32_t v = (*ptr)[0] | ((*ptr)[1] << 8) | ((*ptr)[2] << 16) | ((*ptr)[3] << 24);
    *ptr += 4;
    return v;
}

static uint64_t read_u64(const uint8_t** ptr, const uint8_t* end) {
    if (*ptr + 8 > end) return 0;
    uint64_t v = (uint64_t)(*ptr)[0] | ((uint64_t)(*ptr)[1] << 8) | ((uint64_t)(*ptr)[2] << 16) | ((uint64_t)(*ptr)[3] << 24) |
                 ((uint64_t)(*ptr)[4] << 32) | ((uint64_t)(*ptr)[5] << 40) | ((uint64_t)(*ptr)[6] << 48) | ((uint64_t)(*ptr)[7] << 56);
    *ptr += 8;
    return v;
}

static const char* get_opcode_name(uint8_t op) {
    switch (op) {
        case OP_NOOP: return "NOOP";
        case OP_SET_ENDIAN_LE: return "SET_ENDIAN_LE";
        case OP_SET_ENDIAN_BE: return "SET_ENDIAN_BE";
        case OP_ENTER_STRUCT: return "ENTER_STRUCT";
        case OP_EXIT_STRUCT: return "EXIT_STRUCT";
        case OP_META_VERSION: return "META_VERSION";
        case OP_IO_U8: return "IO_U8";
        case OP_IO_U16: return "IO_U16";
        case OP_IO_U32: return "IO_U32";
        case OP_IO_U64: return "IO_U64";
        case OP_IO_I8: return "IO_I8";
        case OP_IO_I16: return "IO_I16";
        case OP_IO_I32: return "IO_I32";
        case OP_IO_I64: return "IO_I64";
        case OP_IO_F32: return "IO_F32";
        case OP_IO_F64: return "IO_F64";
        case OP_IO_BIT_U: return "IO_BIT_U";
        case OP_IO_BIT_I: return "IO_BIT_I";
        case OP_IO_BIT_BOOL: return "IO_BIT_BOOL";
        case OP_ALIGN_PAD: return "ALIGN_PAD";
        case OP_ALIGN_FILL: return "ALIGN_FILL";
        case OP_STR_NULL: return "STR_NULL";
        case OP_STR_PRE_U8: return "STR_PRE_U8";
        case OP_STR_PRE_U16: return "STR_PRE_U16";
        case OP_STR_PRE_U32: return "STR_PRE_U32";
        case OP_ARR_FIXED: return "ARR_FIXED";
        case OP_ARR_PRE_U8: return "ARR_PRE_U8";
        case OP_ARR_PRE_U16: return "ARR_PRE_U16";
        case OP_ARR_PRE_U32: return "ARR_PRE_U32";
        case OP_ARR_END: return "ARR_END";
        case OP_RAW_BYTES: return "RAW_BYTES";
        case OP_CONST_CHECK: return "CONST_CHECK";
        case OP_CONST_WRITE: return "CONST_WRITE";
        case OP_RANGE_CHECK: return "RANGE_CHECK";
        case OP_SCALE_LIN: return "SCALE_LIN";
        case OP_CRC_16: return "CRC_16";
        case OP_TRANS_ADD: return "TRANS_ADD";
        case OP_TRANS_SUB: return "TRANS_SUB";
        case OP_TRANS_MUL: return "TRANS_MUL";
        case OP_TRANS_DIV: return "TRANS_DIV";
        case OP_CRC_32: return "CRC_32";
        case OP_MARK_OPTIONAL: return "MARK_OPTIONAL";
        case OP_ENUM_CHECK: return "ENUM_CHECK";
        case OP_JUMP_IF_NOT: return "JUMP_IF_NOT";
        default: return "UNKNOWN";
    }
}

int cmd_inspect(int argc, char** argv) {
    if (argc < 3) {
        printf("Usage: cnd inspect <file.il>\n");
        return 1;
    }

    const char* path = argv[2];
    FILE* f = fopen(path, "rb");
    if (!f) {
        printf("Error opening file: %s\n", path);
        return 1;
    }

    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t* data = (uint8_t*)malloc(size);
    fread(data, 1, size, f);
    fclose(f);

    printf("Inspecting: %s (%ld bytes)\n", path, size);

    // Parse Header
    if (size < 16 || memcmp(data, "CNDIL", 5) != 0) {
        printf("Error: Invalid IL file format (Missing Magic)\n");
        free(data);
        return 1;
    }

    uint8_t version = data[5];
    uint16_t str_count = data[6] | (data[7] << 8);
    uint32_t str_offset = data[8] | (data[9] << 8) | (data[10] << 16) | (data[11] << 24);
    uint32_t bc_offset = data[12] | (data[13] << 8) | (data[14] << 16) | (data[15] << 24);

    printf("\n--- Header ---\n");
    printf("Version: %d\n", version);
    printf("String Count: %d\n", str_count);
    printf("String Table Offset: %d\n", str_offset);
    printf("Bytecode Offset: %d\n", bc_offset);

    // Print String Table
    printf("\n--- String Table ---\n");
    if ((long)str_offset < size) {
        const char* ptr = (const char*)(data + str_offset);
        for (int i = 0; i < str_count; i++) {
            printf("[%d] %s\n", i, ptr);
            while (*ptr) ptr++;
            ptr++; // Skip null
            if ((uint8_t*)ptr > data + size) break;
        }
    }

    // Disassemble Bytecode
    printf("\n--- Bytecode ---\n");
    if ((long)bc_offset < size) {
        const uint8_t* ptr = data + bc_offset;
        const uint8_t* end = data + size;
        const uint8_t* start = ptr;

        while (ptr < end) {
            uint32_t offset = (uint32_t)(ptr - start);
            uint8_t op = read_u8(&ptr, end);
            printf("%04X: %-15s", offset, get_opcode_name(op));

            switch (op) {
                case OP_IO_U8:
                case OP_IO_U16:
                case OP_IO_U32:
                case OP_IO_U64:
                case OP_IO_I8:
                case OP_IO_I16:
                case OP_IO_I32:
                case OP_IO_I64:
                case OP_IO_F32:
                case OP_IO_F64:
                case OP_ENTER_STRUCT:
                    printf(" KeyID=%d", read_u16(&ptr, end));
                    break;

                case OP_STR_NULL: {
                    uint16_t k = read_u16(&ptr, end);
                    uint16_t m = read_u16(&ptr, end);
                    printf(" KeyID=%d MaxLen=%d", k, m);
                    break;
                }

                case OP_STR_PRE_U8:
                case OP_STR_PRE_U16:
                case OP_STR_PRE_U32:
                case OP_ARR_PRE_U8:
                case OP_ARR_PRE_U16:
                case OP_ARR_PRE_U32:
                    printf(" KeyID=%d", read_u16(&ptr, end));
                    break;

                case OP_ARR_END:
                    break;

                case OP_IO_BIT_U:
                case OP_IO_BIT_I: {
                    uint16_t k = read_u16(&ptr, end);
                    uint8_t b = read_u8(&ptr, end);
                    printf(" KeyID=%d Bits=%d", k, b);
                    break;
                }
                
                case OP_IO_BIT_BOOL:
                    printf(" KeyID=%d", read_u16(&ptr, end));
                    break;

                case OP_ARR_FIXED: {
                    uint16_t k = read_u16(&ptr, end);
                    uint32_t c = read_u32(&ptr, end);
                    printf(" KeyID=%d Count=%d", k, c);
                    break;
                }

                case OP_RAW_BYTES: {
                    uint16_t k = read_u16(&ptr, end);
                    uint32_t c = read_u32(&ptr, end);
                    printf(" KeyID=%d Count=%d", k, c);
                    break;
                }

                case OP_CONST_WRITE: {
                    uint8_t type = read_u8(&ptr, end);
                    printf(" Type=%s Val=", get_opcode_name(type));
                    if (type == OP_IO_U8) printf("%u", read_u8(&ptr, end));
                    else if (type == OP_IO_U16) printf("%u", read_u16(&ptr, end));
                    else if (type == OP_IO_U32) printf("%u", read_u32(&ptr, end));
                    else if (type == OP_IO_U64) printf("%llu", read_u64(&ptr, end));
                    break;
                }

                case OP_CONST_CHECK: {
                    uint16_t key = read_u16(&ptr, end);
                    uint8_t type = read_u8(&ptr, end);
                    printf(" KeyID=%d Type=%s Val=", key, get_opcode_name(type));
                    if (type == OP_IO_U8 || type == OP_IO_I8) printf("%u", read_u8(&ptr, end));
                    else if (type == OP_IO_U16 || type == OP_IO_I16) printf("%u", read_u16(&ptr, end));
                    else if (type == OP_IO_U32 || type == OP_IO_I32) printf("%u", read_u32(&ptr, end));
                    else if (type == OP_IO_U64 || type == OP_IO_I64) printf("%llu", read_u64(&ptr, end));
                    break;
                }

                case OP_RANGE_CHECK: {
                    uint8_t type = read_u8(&ptr, end);
                    printf(" Type=%s Range=[", get_opcode_name(type));
                    if (type == OP_IO_U8) { printf("%u, %u]", read_u8(&ptr, end), read_u8(&ptr, end)); }
                    else if (type == OP_IO_I8) { printf("%d, %d]", (int8_t)read_u8(&ptr, end), (int8_t)read_u8(&ptr, end)); }
                    else if (type == OP_IO_U16) { printf("%u, %u]", read_u16(&ptr, end), read_u16(&ptr, end)); }
                    else if (type == OP_IO_I16) { printf("%d, %d]", (int16_t)read_u16(&ptr, end), (int16_t)read_u16(&ptr, end)); }
                    else if (type == OP_IO_U32) { printf("%u, %u]", read_u32(&ptr, end), read_u32(&ptr, end)); }
                    else if (type == OP_IO_I32) { printf("%d, %d]", (int32_t)read_u32(&ptr, end), (int32_t)read_u32(&ptr, end)); }
                    else if (type == OP_IO_U64) { printf("%llu, %llu]", read_u64(&ptr, end), read_u64(&ptr, end)); }
                    else if (type == OP_IO_I64) { printf("%lld, %lld]", (int64_t)read_u64(&ptr, end), (int64_t)read_u64(&ptr, end)); }
                    else if (type == OP_IO_F32) { printf("F32 Range"); ptr += 8; }
                    else if (type == OP_IO_F64) { printf("F64 Range"); ptr += 16; }
                    break;
                }

                case OP_SCALE_LIN: {
                    uint64_t fac = read_u64(&ptr, end);
                    uint64_t off = read_u64(&ptr, end);
                    double f, o;
                    memcpy(&f, &fac, 8);
                    memcpy(&o, &off, 8);
                    printf(" Factor=%f Offset=%f", f, o);
                    break;
                }

                case OP_TRANS_ADD:
                case OP_TRANS_SUB:
                case OP_TRANS_MUL:
                case OP_TRANS_DIV:
                    printf(" Val=%lld", (int64_t)read_u64(&ptr, end));
                    break;

                case OP_CRC_16:
                    printf(" Poly=0x%04X Init=0x%04X Xor=0x%04X Flags=%d", 
                        read_u16(&ptr, end), read_u16(&ptr, end), read_u16(&ptr, end), read_u8(&ptr, end));
                    break;

                case OP_CRC_32:
                    printf(" Poly=0x%08X Init=0x%08X Xor=0x%08X Flags=%d", 
                        read_u32(&ptr, end), read_u32(&ptr, end), read_u32(&ptr, end), read_u8(&ptr, end));
                    break;

                case OP_ENUM_CHECK: {
                    uint8_t type = read_u8(&ptr, end);
                    uint16_t count = read_u16(&ptr, end);
                    printf(" Type=%s Count=%d Values=[", get_opcode_name(type), count);
                    for(int i=0; i<count; i++) {
                        if (i > 0) printf(", ");
                        if (type == OP_IO_U8) printf("%u", read_u8(&ptr, end));
                        else if (type == OP_IO_U16) printf("%u", read_u16(&ptr, end));
                        else if (type == OP_IO_U32) printf("%u", read_u32(&ptr, end));
                        else if (type == OP_IO_U64) printf("%llu", read_u64(&ptr, end));
                        else if (type == OP_IO_I8) printf("%d", (int8_t)read_u8(&ptr, end));
                        else if (type == OP_IO_I16) printf("%d", (int16_t)read_u16(&ptr, end));
                        else if (type == OP_IO_I32) printf("%d", (int32_t)read_u32(&ptr, end));
                        else if (type == OP_IO_I64) printf("%lld", (int64_t)read_u64(&ptr, end));
                    }
                    printf("]");
                    break;
                }

                case OP_ALIGN_PAD:
                case OP_ALIGN_FILL:
                    printf(" Align=%d", read_u8(&ptr, end));
                    break;
            }
            printf("\n");
        }
    }

    free(data);
    return 0;
}
