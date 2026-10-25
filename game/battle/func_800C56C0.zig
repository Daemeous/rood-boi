//! Marks two lists of battle slots (src/BATTLE/BATTLE.PRG/5BF94.c).
const psx = @import("common").psx;
const v = @import("views_5BF94");
const c = @import("c_5BF94");

/// orig: func_800C56C0 (src/BATTLE/BATTLE.PRG/5BF94.c)
/// Calls func_800C51B4(1), then sets unk0 of the slots in each index list: 3 for list 0, 2 for list 1.
pub fn func_800C56C0() callconv(.c) void {
    c.func_800C51B4(1);

    // The index lists are u8 entries read with the C's unchecked indexing:
    // the row index i may pass 16 and the slot index may pass 24.
    var i: c_int = 0;
    while (i < @as(c_int, v.g.D_800F4CB1().*)) : (i +%= 1) {
        const slot = psx.at(&v.g.D_800F4C70()[0], i).*;
        psx.at(&v.g.D_800EB9B8().*.?.unk990, slot).unk0 = 3;
    }

    i = 0;
    while (i < @as(c_int, v.g.D_800F4CB0().*)) : (i +%= 1) {
        const slot = psx.at(&v.g.D_800F4C70()[1], i).*;
        psx.at(&v.g.D_800EB9B8().*.?.unk990, slot).unk0 = 2;
    }
}
