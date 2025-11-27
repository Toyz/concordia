#ifndef VM_INTERNAL_H
#define VM_INTERNAL_H

#include "concordia.h"

// IL Reading
uint16_t read_il_u16(cnd_vm_ctx* ctx);
uint8_t read_il_u8(cnd_vm_ctx* ctx);
uint32_t read_il_u32(cnd_vm_ctx* ctx);
uint64_t read_il_u64(cnd_vm_ctx* ctx);

// Data Reading
uint8_t read_u8(const uint8_t* buf);
uint16_t read_u16(const uint8_t* buf, cnd_endian_t endian);
uint32_t read_u32(const uint8_t* buf, cnd_endian_t endian);
uint64_t read_u64(const uint8_t* buf, cnd_endian_t endian);
uint64_t read_bits(cnd_vm_ctx* ctx, uint8_t count);

// Data Writing
void write_u8(uint8_t* buf, uint8_t val);
void write_u16(uint8_t* buf, uint16_t val, cnd_endian_t endian);
void write_u32(uint8_t* buf, uint32_t val, cnd_endian_t endian);
void write_u64(uint8_t* buf, uint64_t val, cnd_endian_t endian);
void write_bits(cnd_vm_ctx* ctx, uint64_t val, uint8_t count);

#endif
