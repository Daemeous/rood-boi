#ifndef ROOD_PC_MEMORY_H
#define ROOD_PC_MEMORY_H
#include <stddef.h>
#include <stdint.h>
#define ROOD_RAM_SIZE (2u * 1024u * 1024u)
#define ROOD_SCRATCH_SIZE 1024u
#define ROOD_OT_END UINT32_C(0x00ffffff)
typedef struct {
    _Alignas(16) unsigned char ram[ROOD_RAM_SIZE];
    _Alignas(16) unsigned char scratch[ROOD_SCRATCH_SIZE];
} RoodMemory;
/* Deliberately reject MMIO and unmapped regions; never dereference guest addresses. */
void* rood_memory_span(RoodMemory* memory, uint32_t address, size_t length);
uint32_t rood_read_le32(const void* bytes);
void rood_write_le32(void* bytes, uint32_t value);
#endif
