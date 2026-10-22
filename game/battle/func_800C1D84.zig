//! Checks whether any of the 24 entries of the battle table at `D_800EB9B8->unk990` has its first byte above 1 (src/BATTLE/BATTLE.PRG/58578.c).
const v = @import("views_58578");

/// orig: func_800C1D84 (src/BATTLE/BATTLE.PRG/58578.c)
/// Returns 0 if any of the 24 entries has `unk0 > 1`, else 1.
pub fn func_800C1D84() callconv(.c) c_int {
    const table = v.g.D_800EB9B8().*.?;
    var i: usize = 0;
    while (i < 24) : (i += 1) {
        if (table.unk990[i].unk0 > 1) {
            return 0;
        }
    }
    return 1;
}
