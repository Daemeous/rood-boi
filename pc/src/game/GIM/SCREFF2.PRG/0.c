#include "common.h"
#include "0.h"
#include "pc/src/game/SLUS_010.40/31724.h"
#include "pc/src/game/BATTLE/BATTLE.PRG/146C.h"
#include "pc/src/game/BATTLE/BATTLE.PRG/573B8.h"
#include "pc/src/game/spriteItem.h"
#include <libgte.h>

void func_800F9800(int arg0)
{
    vs_effects_screenEffectOverlay->unk2 = arg0;
    if (vs_effects_screenEffectOverlay->unk1 == 0) {
        if (arg0 != 0) {
            vs_effects_screenEffectOverlay->unk1 = 1U;
            vs_render_reserveTexturePages(0x1B, 5, 6);
        }
    } else if (arg0 == 0) {
        vs_effects_screenEffectOverlay->unk1 = 0U;
        vs_render_retireTexturePages(0x1B, 5, 6);
    }
}

void func_800F986C(void)
{
    int temp_v0;
    int var_s0;
    int x;
    int var_s2;
    int y;
    u_long* temp_s4;

    temp_s4 = vs_scratch.unk4;

    for (y = 192; y >= 0; y -= 32) {
        for (var_s2 = 0x100; var_s2 >= 0; var_s2 -= 64) {
            for (x = 0x40; x >= 0; x -= 0x20) {
                int s5 = (((var_s2 + 0x2C0) & 0x3FF) >> 6) | 0x170;

                if (x == 0x40) {
                    vs_battle_spriteItem(temp_s4, 0x160, (var_s2 + 2) | ((y + 30) << 16),
                        0x40000 | 0x40, s5 | 0xE1000200, (y + 0x1C) << 8);
                } else {
                    vs_battle_spriteItem(temp_s4, 0x160, (var_s2 + x + 0x1E) | ((y + 2) << 16),
                        0x1C0004, s5 | 0xE1000200, (x + 0x1C) | (y << 8));
                }
            }
        }
    }

    for (y = 192; y >= 0; y -= 32) {
        for (x = 0x120; x >= 0; x -= 32) {
            for (var_s0 = 0; var_s0 < 3; ++var_s0) {
                int brightness, xy, wh;

                switch (var_s0) {
                case 0:
                    brightness = 0x160;
                    xy = (x + 2) | ((y + 2) << 16);
                    wh = 0x1C001C;
                    break;
                case 1:
                    brightness = 0x160;
                    xy = ((x - 2) & 0xFFFF) | ((y - 2) << 16);
                    wh = 0x200020;
                    break;
                default:
                    brightness = 0x60;
                    xy = x | (y << 0x10);
                    wh = 0x200020;
                    break;
                }

                temp_v0 = (((x & 0x1C0) + 0x2C0) & 0x3FF) >> 6;
                temp_v0 |= var_s0 == 0 ? 0x170 : 0x110;

                vs_battle_spriteItem(
                    temp_s4, brightness, xy, wh, temp_v0 | 0xE1000200, (x & 0x20) | (y << 8));
            }
        }
    }

    /* The picture's own screen area (the sprites above draw in it) and, drawn last, the display's again: items drawn in
     * the reverse of the order they are made in, so the last is made first. */
    var_s0 = vs_effects_screenEffectOverlay->unk2 - 3;
    vs_battle_maskItem(temp_s4 + var_s0, 1);
    vs_battle_tileFlatItem(temp_s4 + var_s0, 0, 0, 0, 0x140 | (0xE0 << 16));
    vs_battle_areaItem(temp_s4 + var_s0, 704, 256, 320, 224, 1, 704, 256);

    x = vs_main_frameBuf == 0 ? 0x140 : 0;

    vs_battle_maskItem(temp_s4, 0);
    vs_battle_areaItem(temp_s4, x, 0, 320, 224, 1, x, 0);
}

void func_800F9BC0(short arg0, short arg1)
{
    vs_effects_screenEffectOverlay->unk4 = arg0;
    vs_effects_screenEffectOverlay->unk6 = arg1;
}
