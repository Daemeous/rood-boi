//! Copies the five colour-curve entries of the current block-5 record for a
//! curve set (src/BATTLE/BATTLE.PRG/5BF94.c).
const psx = @import("common").psx;
const v = @import("views_5BF94");

/// orig: func_800D5088 (src/BATTLE/BATTLE.PRG/5BF94.c)
/// Writes the R, G and B curve bytes of the five curve entries selected for `arg0`'s curve set.
pub fn func_800D5088(arg0: ?*v.D_800F53B8_t) callconv(.c) c_int {
    var i: usize = 0;
    while (i < 5) : (i += 1) {
        const dst = psx.at(
            &psx.get(v.g.D_800F569C().*.?.block5Data).?.unk4,
            v.g.D_800EC368()[arg0.?.unkD1C.unk18.unk2][i][3],
        );
        dst.rCurve = v.g.D_800EC368()[arg0.?.unkD1C.unk18.unk2][i][0];
        dst.gCurve = v.g.D_800EC368()[arg0.?.unkD1C.unk18.unk2][i][1];
        dst.bCurve = v.g.D_800EC368()[arg0.?.unkD1C.unk18.unk2][i][2];
    }
    return 1;
}
