//! Sets the battle flag D_800F227A (pc/src/game/BATTLE/BATTLE.PRG/2842C.c).
const v = @import("views_2842C");

/// orig: vs_battle_suppressAshleyStatusLabels (pc/src/game/BATTLE/BATTLE.PRG/2842C.c)
/// Sets the flag D_800F227A to 1.
pub fn vs_battle_suppressAshleyStatusLabels() callconv(.c) void {
    v.g.D_800F227A().* = 1;
}
