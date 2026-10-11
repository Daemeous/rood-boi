/* Native translations of the room navigation setup in BATTLE/7E05C.s. */
#include "psx_types.h" /* psx_load/psx_store: 4-byte guest pointers */
extern void* vs_main_allocHeap(unsigned);
extern void* vs_main_allocHeapR(unsigned);
extern void vs_main_freeHeapR(void*);
extern void func_800E4288(void*, unsigned);
extern void func_800E6098(void);
extern void func_800DC638(void);
extern unsigned func_8008B6FC(void); /* 146C.c: int, the section's guest address */
extern unsigned _getCollisionMapDimensions(int);
extern short func_8008E4AC(void);
extern unsigned* func_8008B764(unsigned, unsigned, int);
extern unsigned short* func_8008D438(unsigned, unsigned, int);
extern int func_8008DC7C(int, int);
/* Guest words at 0x800F58xx hold 32-bit guest addresses: stored as words so
 * a write never spills into the next one (the game declares D_800F58C0 int). */
extern unsigned D_800F58C0, D_800F58D8, D_800F58C8, D_800F5908, D_800F5910, D_800F58CC,
    D_800F58BC, D_800F58B8, D_800F58D0;
extern void pc_unimplemented(const char*);

int func_800E71DC(int x, int z)
{
    x *= 128;
    z *= 128;
    int highest = func_8008DC7C(x + 64, z + 64), center = 0;
    unsigned blocked = 0;
    for (unsigned i = 0; i < 16; ++i) {
        int height = func_8008DC7C(x + (i >> 2) * 32, z + (i & 3) * 32);
        if (i == 10)
            center = height;
        if (height < 0)
            blocked |= 1u << i;
        /* Compare signed 15-bit heights, preserving the separate high flags. */
        int candidate = (int)((unsigned)height << 17) >> 17;
        int current = (int)((unsigned)highest << 17) >> 17;
        if (current < candidate)
            highest = height;
    }
    if (!blocked)
        return highest;
    unsigned open = ~blocked;
    for (unsigned i = 0; i < 10; ++i) {
        if (i == 3 || i == 7)
            continue;
        unsigned square = 0x33u << i;
        if ((open & square) == square)
            return highest;
    }
    return center;
}
void func_800E6B4C(void)
{
    /* The PS1 temporarily used this allocation as its stack. Keep allocation
     * ordering, but native C uses the host stack and never stores a host SP. */
    void* temporary = vs_main_allocHeapR(0x8000);
    if (!temporary)
        return;
    func_800E6098();
    vs_main_freeHeapR(temporary);
}
void func_800E6F9C(void)
{
    unsigned address = func_8008B6FC();
    unsigned char* section = psx_to_ptr(unsigned char*, address);
    D_800F58C0 = address;
    D_800F58D8 = address + 0x400;
    D_800F58C8 = address + 0x440;
    D_800F5908 = address + 0x450;
    D_800F5910 = address + 0x454;
    D_800F58CC = address + 0x498;
    unsigned char* grid = vs_main_allocHeap(0x1038);
    if (!grid)
        pc_unimplemented("navigation allocation failed");
    D_800F58BC = psx_to_int(unsigned, grid);
    func_800E4288(grid, 0x1038);
    D_800F58B8 = psx_to_int(unsigned, grid + 0x38);
    D_800F58D0 = psx_to_int(unsigned, grid + 0x838);
    unsigned short* heights = (unsigned short*)(grid + 0x838);
    unsigned short* cells = (unsigned short*)(grid + 0x38);
    unsigned size = _getCollisionMapDimensions(0);
    unsigned packed = ((size & 0xffff) >> 4) | (size >> 12);
    *(unsigned short*)(grid + 4) = packed;
    *(unsigned short*)grid = packed;
    *(int*)(grid + 0x14) = -(func_8008E4AC() * 64);
    unsigned flags = 0;
    for (unsigned i = 0; i < 1024; ++i) {
        unsigned x = i & 31, z = i >> 5;
        unsigned* tile = func_8008B764(x, z, 0);
        if (tile) {
            unsigned raw = *tile;
            flags = ((raw >> 23) & 15) | (((raw >> 27) & 1) << 4)
                  | (((raw >> 17) & 1) << 5) | (((raw >> 28) & 1) << 6)
                  | (((raw >> 31) & 1) << 11) | (((raw >> 30) & 1) << 12)
                  | (((raw >> 29) & 1) << 13);
            heights[i] = (func_800E71DC(x, z) >> 6) & 0x3ff;
            unsigned short* object = func_8008D438(x, z, 0);
            if (object && object[3] < 16 && object[4] < 2)
                flags |= 1u << (object[4] + 8);
        } else
            flags |= 0x8000;
        cells[i] = flags;
    }
    func_800E6B4C();
    if (!section)
        *(unsigned short*)(grid + 8) = 150;
    else {
        unsigned settings = *psx_to_ptr(unsigned*, D_800F5908);
        *(unsigned*)grid = *(unsigned*)(section + 0x494);
        grid[0xd] = settings >> 29;
        *(unsigned short*)(grid + 8) = (settings << 3) >> 24;
        grid[0xa] = (settings << 11) >> 31;
    }
    func_800DC638();
}

extern int func_8008D2C0(void*);
/* 73C84.s: enable navigation links associated with active room exits.
 * Retain the register mutation across iterations, including variable shifts. */
void func_800DC638(void)
{
    unsigned short exits[13][4];
    int count = func_8008D2C0(exits);
    if (count < 0 || count > 13)
        pc_unimplemented("room exit count exceeds original buffer");
    unsigned active = 0;
    for (int i = count - 1; i >= 0; --i)
        active |= 1u << (exits[i][3] & 31);
    if (!D_800F58C0)
        return;
    unsigned one = 0;
    int one_known = 0;
    for (unsigned offset = 0; offset < 0x400; offset += 4) {
        unsigned* entry = psx_to_ptr(unsigned*, D_800F58C0 + offset);
        unsigned value = *entry;
        if (value & 0x04000400) {
            value |= 0x80008000;
            unsigned x0 = (value >> 5) & 31, z0 = value & 31;
            unsigned x1 = (value >> 21) & 31, z1 = (value >> 16) & 31;
            unsigned bit0 = (value >> 13) & 3, bit1 = (value >> 29) & 3;
            for (unsigned i = 0; i < 8; ++i) {
                unsigned link = psx_to_ptr(unsigned*, D_800F5910)[i];
                unsigned x = link & 31, z = (link >> 5) & 31;
                if (value & 0x400) {
                    one = 1;
                    one_known = 1;
                    if (x0 == x) {
                        bit0 = one << (bit0 & 31);
                        if (z0 == z) {
                            bit0 &= active;
                            if (bit0)
                                value &= ~0x8000u;
                        }
                    }
                }
                if (!one_known && active && (value & 0x04000000))
                    pc_unimplemented("navigation relies on original stack address in v1");
                bit1 = one << (bit1 & 31);
                if (value & 0x04000000) {
                    bit1 &= active;
                    if (x1 == x && z1 == z && bit1)
                        value &= 0x7fffffff;
                }
                one = (i + 1 < 8);
                one_known = 1;
            }
            *entry = value;
        }
        one = (offset + 4 < 0x400);
        one_known = 1;
    }
}
