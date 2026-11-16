//! The stat bar with its value digits and label (pc/src/game/BATTLE/BATTLE.PRG/5BF94.c).
const psx = @import("common").psx;
const v = @import("views_5BF94");
const c = @import("c_5BF94");
const sprite_item = @import("common").sprite_item;

/// orig: _renderStatBar (pc/src/game/BATTLE/BATTLE.PRG/5BF94.c)
/// Draws stat bar `colorIndex` at `current` of `max`, with its value digits and label.
pub fn renderStatBar(colorIndex: c_int, current: c_int, max: c_int) callconv(.c) void {
    var cur = current;

    // vs_scratch.unk4 - 0xC: a guest address minus 12 bytes, as the C computes it.
    const base_addr: u32 = psx.addr(psx.get(v.g.vs_scratch().unk4).?);
    const prim: [*c]c_uint = @ptrFromInt(@as(usize, base_addr -% 12));
    var xy: c_int = psx.at(v.g.vs_battle_hudGaugePositions(), colorIndex).* -% v.g.vs_battle_hudSlideOffset().*;

    if (max != 0) {
        // (current << 6) is taken as two's complement, as GCC does.
        const shifted: c_int = @bitCast(@as(c_uint, @bitCast(cur)) << 6);
        const bar: c_int = @divTrunc(shifted +% max -% 1, max);
        c.vs_battle_renderStatBar(colorIndex, bar, prim, xy);

        if ((colorIndex & 2) != 0) {
            xy +%= 54 | (3 << 0x10);
            if (colorIndex == 3) {
                cur = (cur +% 0xFF) >> 8;
            }
        } else {
            // (u_short)-8 << 16 and (u_short)-7 | (u_short)-2 << 16, as int bit patterns.
            const maxOffset: c_int = @bitCast(@as(c_uint, 0xFFF8_0038));
            const digitOffset: c_int = @bitCast(@as(c_uint, 0xFFFE_FFF9));

            xy = c.vs_battle_renderValue(0, xy +% maxOffset, max, null);
            c._renderDigit(2, xy, 0, prim);
            xy +%= digitOffset;
        }
        _ = c.vs_battle_renderValue(1, xy, cur, null);

        // hud_label_opaque: the label opaque (0x80), or the original's semi-transparent sprite (0x180).
        const solid = psx.Global(c_int, "pc_hud_label_opaque").ptr().* != 0;
        _ = sprite_item.sprite(prim, if (solid) 0x80 else 0x180, psx.at(v.g.vs_battle_hudGaugeLabelPositions(), colorIndex).* -% v.g.vs_battle_hudSlideOffset().*, @as(c_int, psx.at(v.g.vs_battle_hudGaugeLabelWidths(), colorIndex).*) | 0x90000, sprite_item.page, @bitCast(@as(c_uint, psx.at(v.g.D_800EBC00(), colorIndex).*) | 0x37F6_0000));
    }
}
