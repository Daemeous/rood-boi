//! Copies the R, G and B curves of the selected colour curve into the two
//! entries of the block 5 data (src/BATTLE/BATTLE.PRG/5BF94.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_5BF94");

/// orig: func_800D4FB4 (src/BATTLE/BATTLE.PRG/5BF94.c)
/// Writes the R, G and B curves of the current curve kind into both block 5 entries.
pub fn func_800D4FB4(arg0: ?*v.D_800F53B8_t) callconv(.c) c_int {
    var i: usize = 0;
    while (i < 2) : (i += 1) {
        const dst = psx.at(&psx.get(v.g.D_800F569C().*.?.block5Data).?.unk4, i);
        const kind: usize = @as(usize, arg0.?.unkD1C.unk18.unk2);
        const curve = v.g.D_800EC330();
        dst.rCurve = curve[kind][i][0];
        dst.gCurve = curve[kind][i][1];
        dst.bCurve = curve[kind][i][2];
    }

    return 1;
}
