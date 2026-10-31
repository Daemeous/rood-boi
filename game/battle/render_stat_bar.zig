//! The stat bar with its value digits and label (src/BATTLE/BATTLE.PRG/5BF94.c).
const psx = @import("common").psx;
const v = @import("views_5BF94");
const c = @import("c_5BF94");

/// orig: _renderStatBar (src/BATTLE/BATTLE.PRG/5BF94.c)
/// Draws stat bar `colorIndex` at `current` of `max`, with its value digits and label.
pub fn renderStatBar(colorIndex: c_int, current: c_int, max: c_int) callconv(.c) void {
    var cur = current;

    // vs_scratch.unk4 - 0xC: a guest address minus 12 bytes, as the C computes it.
    const base_addr: u32 = psx.addr(psx.get(v.g.vs_scratch().unk4).?);
    const prim: [*c]c_uint = @ptrFromInt(@as(usize, base_addr -% 12));
    var xy: c_int = psx.at(v.g.D_800EBBDC(), colorIndex).* -% v.g.D_800EB9B0().*;

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

        const sprite = c.vs_battle_setSpriteDefaultTexPage(
            0x80,
            psx.at(v.g.D_800EBBEC(), colorIndex).* -% v.g.D_800EB9B0().*,
            @as(c_int, psx.at(v.g.D_800EBBFC(), colorIndex).*) | 0x90000,
            prim,
        );
        sprite[4] = @as(c_uint, psx.at(v.g.D_800EBC00(), colorIndex).*) | 0x37F6_0000;
    }
}
