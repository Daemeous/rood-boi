/* SCREFF2/3D8.s: deform polygon XY words in the existing ordering table.
 * This is an overlay-specific native entry, never a guest function pointer. */
#include <sys/types.h>
#include <libgte.h>
#include <inline_c.h>
#include "src/GIM/SCREFF2.PRG/0.h"
extern unsigned short D_80040C18[];
extern void pc_unimplemented(const char*);
static unsigned* warp_link(unsigned address)
{
    if (address < 0x200000)
        return psx_to_ptr(unsigned*, address | 0x80000000);
    if (sizeof(void*) == 4 && address >= 0x400000 && address < 0x1000000)
        return psx_to_ptr(unsigned*, address); /* 32-bit build: native below 16 MB */
    pc_unimplemented("screen warp OT pointer");
    return 0;
}
static unsigned warped_position(unsigned xy, const unsigned short* factors)
{
    int x = (short)xy - 160, y = (short)(xy >> 16) - 112;
    MTC2(x, 9);
    MTC2(y, 10);
    gte_sqr0();
    unsigned distance = MFC2(25) + MFC2(26), radius = 0;
    MTC2(distance, 30);
    if (distance) {
        unsigned leading = MFC2(31) & ~1u;
        /* Preserve this routine's MIPS variable arithmetic shift, including
         * its masked shift count for tiny nonzero radii. */
        int index = ((int)distance >> ((24 - leading) & 31)) - 64;
        unsigned value = (unsigned)D_80040C18[index] << (((31 - leading) >> 1) & 31);
        radius = (int)value >> 12;
        if (radius >= 444)
            radius = 443;
    }
    unsigned scale = factors[radius];
    int wx = (int)((unsigned)x * scale) >> 13, wy = (int)((unsigned)y * scale) >> 13;
    return (unsigned short)(wx + 160) | ((unsigned)(unsigned short)(wy + 112) << 16);
}
void pc_screff2_warp_packets(
    unsigned* ot, unsigned short base, short growth, unsigned short* factors)
{
    int scale = (unsigned)base << 4, multiplier = growth + 4096;
    for (unsigned i = 0; i < 444; ++i) {
        factors[i] = scale >> 3;
        scale = (int)((unsigned)scale * (unsigned)multiplier) >> 12;
    }
    unsigned saved = ot[0];
    ot[0] = 0xffffffff;
    unsigned* packet = ot + 0x7fe;
    for (unsigned links = 0; links < 100000; ++links) {
        unsigned words = packet[0] >> 24;
        for (unsigned i = 1; i <= words; ++i) {
            unsigned type = (packet[i] >> 26) - 8;
            if (type >= 48)
                continue;
            if (type < 8) {
                unsigned stride = 1 + (type & 1) + ((type >> 2) & 1);
                unsigned count = 3 + ((type >> 1) & 1);
                unsigned start = i + 1;
                if (start + (count - 1) * stride > words)
                    pc_unimplemented("truncated screen warp polygon");
                for (unsigned v = 0; v < count; ++v)
                    packet[start + v * stride] =
                        warped_position(packet[start + v * stride], factors);
            }
            break;
        }
        unsigned next = packet[0] & 0xffffff;
        if (next == 0xffffff) {
            ot[0] = saved;
            return;
        }
        packet = warp_link(next);
        if ((packet[0] & 0xffffff) == 0xffffff) {
            ot[0] = saved;
            return;
        }
    }
    pc_unimplemented("screen warp OT cycle");
}
void pc_screff2_warp(void)
{
    pc_screff2_warp_packets(
        psx_load(unsigned*, 0x1f800004), D_800EB9B4->unk4, D_800EB9B4->unk6, (void*)0x1f800088);
}
