/* SCREFF2/3D8.s: deform the polygons of the frame's draw items placed in the ordering table's slots.
 * This is an overlay-specific native entry, never a guest function pointer. */
#include <sys/types.h>
#include <libgte.h>
#include "gte_value_core.h"
#include "view_state.h"
#include "draw_items.h"
#include "pc/src/game/GIM/SCREFF2.PRG/0.h"
int pc_root_table(int index); /* boot_sdk.c */
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
        unsigned value = (unsigned)pc_root_table(index) << (((31 - leading) >> 1) & 31);
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
void pc_screff2_warp_packets(
    unsigned* ot, unsigned short base, short growth, unsigned short* factors)
{
    int scale = (unsigned)base << 4, multiplier = growth + 4096;
    for (unsigned i = 0; i < 444; ++i) {
        factors[i] = scale >> 3;
        scale = (int)((unsigned)scale * (unsigned)multiplier) >> 12;
    }
    { /* (the runs placed in the slots from the table's start to its entry 0x7fe: their slot is their place) */
        static int firsts[8192], counts[8192];
        int runs = pc_frame_runs_between(ot, ot + 0x7fe, firsts, counts, 8192);
        for (int i = 0; i < runs; i++)
            warp_items(firsts[i], counts[i], factors);
    }
}
void pc_screff2_warp(void)
{
    pc_screff2_warp_packets(
        (unsigned*)PC_OT_BASE, vs_effects_screenEffectOverlay->unk4, vs_effects_screenEffectOverlay->unk6, VS_PAD_AT(char, 0x88));
}
