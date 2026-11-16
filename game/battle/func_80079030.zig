//! The action's phase 8: the re-cast (pc/src/game/BATTLE/BATTLE.PRG/146C.c).
const c = @import("c_146C");

/// orig: func_80079030 (pc/src/game/BATTLE/BATTLE.PRG/146C.c)
/// Starts the cast again (vs_battle_startActionPresentation).
pub fn func_80079030() callconv(.c) void {
    c.vs_battle_startActionPresentation();
}
