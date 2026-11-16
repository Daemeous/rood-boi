//! Draws a status icon's sprite pair into the battle scratch packet (pc/src/game/BATTLE/BATTLE.PRG/5BF94.c).
const psx = @import("common").psx;
const v = @import("views_5BF94");
const c = @import("c_5BF94");
const sprite_item = @import("common").sprite_item;

/// orig: vs_battle_drawSlidingStatusIcon (pc/src/game/BATTLE/BATTLE.PRG/5BF94.c)
/// Writes the sprite for status icon `icon` at offset `dx` on row `row`, splitting it across a second sprite when it wraps.
pub fn vs_battle_drawSlidingStatusIcon(icon: c_int, dx: c_int, row: c_int) callconv(.c) void {
    if (icon != 0xFF and v.g.vs_battle_hudSlideOffset().* == 0) {
        var s2: c_int = @as(c_int, psx.at(v.g.vs_battle_statusIconTexOffsets(), icon).*);
        var y: c_int = (row *% 0x10) +% 0xA0;
        var s1: c_int = undefined;
        if (dx < 0) {
            s2 -%= dx;
            s1 = dx +% 0x10;
        } else {
            y +%= dx;
            s1 = 0x10 -% dx;
        }

        // The shift count is the low 5 bits of icon, as the x86 shift masks it.
        const sh: u5 = @truncate(psx.conv(c_uint, icon));
        _ = sprite_item.sprite(@ptrFromInt(@intFromPtr(psx.get(v.g.vs_scratch().unk4)) -% 4), 0x80, y | 0x80000, s1 | 0x100000, sprite_item.page, s2 | (if ((((@as(c_int, 0x0F0F906A) >> sh) & 1)) != 0) @as(c_int, 0x37F90000) else 0x37F80000));

        if ((icon & 0x10) != 0 and (y & 8) == 0) {
            s2 = ((icon & 3) *% 8) +% 0x3068;
            if (s1 >= 9) {
                if ((y & 7) != 0) {
                    y +%= 8;
                    s1 -%= 8;
                } else {
                    y = (y -% 8) +% s1;
                    s1 = 8;
                }
            } else {
                s2 = (((icon & 3) *% 8) +% 0x3070) -% s1;
            }

            _ = sprite_item.sprite(@ptrFromInt(@intFromPtr(psx.get(v.g.vs_scratch().unk4)) -% 4), 0x80, y | 0x80000, s1 | 0x80000, sprite_item.page, s2 | 0x37FF0000);
        }
    }
}
