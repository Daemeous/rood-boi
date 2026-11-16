/* SCREFF2/3D8.s: deform polygon XY words in the existing ordering table.
 * This is an overlay-specific native entry, never a guest function pointer. */
#include <sys/types.h>
#include <libgte.h>
#include "gte_value_core.h"
#include "view_state.h"
#include "draw_items.h"
#include "pc/src/game/GIM/SCREFF2.PRG/0.h"
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
    vs_gte_ctx ctx; /* this call's own registers */
    vs_gte_ctx_init(&ctx);
    vs_gte_mtc2(&ctx, 9, x);
    vs_gte_mtc2(&ctx, 10, y);
    vs_gte_cop2(&ctx, 0x0A00428); /* gte_sqr0 */
    unsigned distance = vs_gte_mfc2(&ctx, 25) + vs_gte_mfc2(&ctx, 26), radius = 0;
    vs_gte_mtc2(&ctx, 30, distance);
    if (distance) {
        unsigned leading = vs_gte_mfc2(&ctx, 31) & ~1u;
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
/* The polygon items a marker places (pc_frame_place), deformed as the packets they replace were: their corners (a tile or sprite, like a
 * rectangle packet, is not). */
static void warp_items(int first, int count, const unsigned short* factors)
{
    for (int index = first; index < first + count; ++index) {
        pc_item* item = pc_frame_item(index);
        if (!item || item->kind != PC_ITEM_POLY)
            continue;
        for (int v = 0; v < item->verts; ++v) {
            unsigned xy = warped_position((unsigned short)item->x[v] | ((unsigned)(unsigned short)item->y[v] << 16), factors);
            item->x[v] = (short)xy, item->y[v] = (short)(xy >> 16);
        }
    }
}
/* The polygon a packet holds after its draw mode words (a rectangle, a line or anything else is left as it is). */
static void warp_packet(unsigned* packet, unsigned words, const unsigned short* factors)
{
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
                packet[start + v * stride] = warped_position(packet[start + v * stride], factors);
        }
        break;
    }
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
        int first, placed;
        if (pc_frame_marker(packet, &first, &placed))
            warp_items(first, placed, factors);
        else
            warp_packet(packet, words, factors);
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
        (unsigned*)PC_OT_BASE, vs_effects_screenEffectOverlay->unk4, vs_effects_screenEffectOverlay->unk6, (void*)PC_PAD(0x88));
}
