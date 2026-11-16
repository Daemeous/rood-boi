//! Clears the first byte of the battle global vs_battle_targetSphere, if it is set (pc/src/game/BATTLE/BATTLE.PRG/573B8.c).
const v = @import("views_573B8");

/// orig: func_800C06E0 (pc/src/game/BATTLE/BATTLE.PRG/573B8.c)
/// Clears the first byte of the object held in vs_battle_targetSphere, if there is one.
pub fn func_800C06E0() callconv(.c) void {
    const p = v.g.vs_battle_targetSphere().* orelse return;
    p.unk0[0].unk0 = 0;
}
