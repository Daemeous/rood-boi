//! Clears the first byte of the battle global D_800EB9B8, if it is set (src/BATTLE/BATTLE.PRG/573B8.c).
const v = @import("views_573B8");

/// orig: func_800C06E0 (src/BATTLE/BATTLE.PRG/573B8.c)
/// Clears the first byte of the object held in D_800EB9B8, if there is one.
pub fn func_800C06E0() callconv(.c) void {
    const p = v.g.D_800EB9B8().* orelse return;
    p.unk0.unk0 = 0;
}
