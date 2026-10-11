#include "memory.h"
void* rood_memory_span(RoodMemory* memory, uint32_t address, size_t length)
{
    uint32_t physical = address;
    if (address >= UINT32_C(0x80000000) && address < UINT32_C(0xc0000000))
        physical &= UINT32_C(0x1fffffff);
    if (physical < ROOD_RAM_SIZE && length <= ROOD_RAM_SIZE - physical)
        return memory->ram + physical;
    if (physical >= UINT32_C(0x1f800000) && physical < UINT32_C(0x1f800400)) {
        size_t offset = physical - UINT32_C(0x1f800000);
        if (length <= ROOD_SCRATCH_SIZE - offset)
            return memory->scratch + offset;
    }
    return NULL;
}
uint32_t rood_read_le32(const void* bytes)
{
    const unsigned char* p = bytes;
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16)
         | ((uint32_t)p[3] << 24);
}
void rood_write_le32(void* bytes, uint32_t value)
{
    unsigned char* p = bytes;
    p[0] = value;
    p[1] = value >> 8;
    p[2] = value >> 16;
    p[3] = value >> 24;
}
