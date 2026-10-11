#include "memory.h"
#include <stdio.h>
#include <stdlib.h>
#define CHECK(x) do { if (!(x)) { fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #x); return 1; } } while (0)
static RoodMemory memory;
int main(void)
{
    void* packet = rood_memory_span(&memory, 0x80001000, 16);
    CHECK(packet);
    CHECK(packet == rood_memory_span(&memory, 0xa0001000, 16));
    CHECK(packet == rood_memory_span(&memory, 0x1000, 16));
    CHECK(!rood_memory_span(&memory, 0x1fffff, 2));
    CHECK(!rood_memory_span(&memory, 0x1f8003ff, 2));
    CHECK(!rood_memory_span(&memory, 0x1f801810, 4));
    CHECK(!rood_memory_span(&memory, 0xffffffff, 4));
    CHECK(!rood_memory_span(&memory, 0x80001000, SIZE_MAX));
    void* scratch = rood_memory_span(&memory, 0x1f800088, 4);
    CHECK(scratch);
    rood_write_le32(scratch, 0x12345678);
    CHECK(rood_read_le32(scratch) == 0x12345678);
    /* A real PS1 tag: high byte is length, low 24 bits are the next guest address. */
    rood_write_le32(packet, (3u << 24) | 0x2000);
    void* next = rood_memory_span(&memory, rood_read_le32(packet) & ROOD_OT_END, 4);
    CHECK(next == memory.ram + 0x2000);
    CHECK((rood_read_le32(packet) >> 24) == 3);
    rood_write_le32(next, ROOD_OT_END);
    CHECK((rood_read_le32(next) & ROOD_OT_END) == ROOD_OT_END);
    CHECK((char)0xff > 0);
    printf(
        "PASS: %zu-bit host; RAM aliases, scratchpad, bounds, packed tags, unsigned char\n",
        sizeof(void*) * 8);
    return 0;
}
