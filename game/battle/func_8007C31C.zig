//! Runs func_80086754 on the player's stats (src/BATTLE/BATTLE.PRG/146C.c).
const psx = @import("common").psx;
const v = @import("views_146C");
const c = @import("c_146C");

/// orig: func_8007C31C (src/BATTLE/BATTLE.PRG/146C.c)
/// Calls func_80086754(0, ...) with the character's stats block.
pub fn func_8007C31C() callconv(.c) void {
    c.func_80086754(0, @ptrCast(psx.get(v.g.vs_battle_characterState().*.?.stats)));
}
