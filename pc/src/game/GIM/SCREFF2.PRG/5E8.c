#include "common.h"
#include "0.h"
#include "pc/src/game/SLUS_010.40/31724.h"
#include "pc/src/game/BATTLE/BATTLE.PRG/146C.h"
#include "pc/src/game/BATTLE/BATTLE.PRG/573B8.h"
#include "pc/src/game/spriteItem.h"
#include <libgte.h>

void func_800F9DE8(int arg0) { vs_effects_screenEffectOverlay->unk8 = arg0; }

static int func_800F9DF8(int arg0, int arg1)
{
    int var_s1;

    var_s1 = 8 - ((arg0 + 8) & 0xF);
    if (var_s1 < 0) {
        var_s1 = -var_s1;
    }
    var_s1 += 0x40;
    return ((((((rcos(arg0 << 6) * arg1 * 0xB) >> 0xF) * var_s1) >> 6) + 0xA0) & 0xFFFF)
         | ((((((rsin(arg0 << 6) * arg1) >> 0xC) * var_s1) >> 6) + 0x70) << 0x10);
}

/* The border's ring: four semi-transparent triangles from the corner `arg0` to the points arg1 + 1 and arg1 round the edge. */
static void func_800F9EBC(int arg0, int arg1, void** arg3)
{
    int var_s2;

    for (var_s2 = arg1; var_s2 < arg1 + 4; ++var_s2) {
        const int rgb[3] = { 0xFFFFFF, 0xA0A0A0, 0xA0A0A0 };
        const int xy[3] = { arg0, func_800F9DF8(var_s2 + 1, 0x80), func_800F9DF8(var_s2, 0x80) };

        vs_battle_shadedItem((unsigned*)arg3, 3, 1, rgb, xy);
    }
}

void func_800F9FB8(void)
{
    int i;
    void** temp_s4;

    temp_s4 = vs_scratch.unk8;
    func_800F9EBC(0xE00140, 6, temp_s4);
    func_800F9EBC(0xE00000, 0x16, temp_s4);
    func_800F9EBC(0, 0x26, temp_s4);
    func_800F9EBC(0x140, 0x36, temp_s4);

    for (i = 0; i < 0x40; ++i) {
        /* the glow's quad, drawn in the additive mode: the mode is made after it (drawn first) */
        const int rgb[4] = { 0xA0A0A0, 0xA0A0A0, 0, 0 };
        const int xy[4] = { func_800F9DF8(i + 1, 0x80), func_800F9DF8(i, 0x80), func_800F9DF8(i + 1, 0x60),
            func_800F9DF8(i, 0x60) };

        vs_battle_shadedItem((unsigned*)temp_s4, 4, 1, rgb, xy);
        vs_battle_modeItem((unsigned*)temp_s4, 0x220);
    }
}
