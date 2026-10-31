//! Fills the per-slot `unk20` values of the block at `state` from the block-9 slot count
//! (src/BATTLE/BATTLE.PRG/5BF94.c).
const psx = @import("common").psx;
const v = @import("views_5BF94");

/// orig: func_800D49A4 (src/BATTLE/BATTLE.PRG/5BF94.c)
/// Sets every slot's unk20 to -0x80 for states 6..21, 0x21 and 0x24, or to 0x80 for state 0x38 with byte 3 0x2B.
pub fn func_800D49A4(state: ?*v.D_800F53B8_t) callconv(.c) c_int {
    const self = state.?;
    const unk38: c_int = self.unkD1C.unk38;

    if (psx.conv(u16, unk38 -% 6) < 16 or unk38 == 0x21 or unk38 == 0x24) {
        var i: c_int = 0;
        // The slot count is re-read each pass, as the C's loop condition does.
        while (i < @as(c_int, psx.get(v.g.D_800F569C().*.?.block9Data).?.unk15)) : (i +%= 1) {
            psx.at(&self.unk1C, i).unk20 = -0x80;
        }
    } else if (unk38 == 0x38 and self.unk10[3] == 0x2B) {
        var i: c_int = 0;
        while (i < @as(c_int, psx.get(v.g.D_800F569C().*.?.block9Data).?.unk15)) : (i +%= 1) {
            psx.at(&self.unk1C, i).unk20 = 0x80;
        }
    }
    return 1;
}
