//! Battle unit 146C: forwards to vs_battle_commitSelectedAction with 0 (pc/src/game/BATTLE/BATTLE.PRG/146C.c).
const c = @import("c_146C");

/// orig: func_800719DC (pc/src/game/BATTLE/BATTLE.PRG/146C.c)
/// Calls vs_battle_commitSelectedAction with 0; its argument is unused.
pub fn func_800719DC(_: c_int) callconv(.c) void {
    c.vs_battle_commitSelectedAction(0);
}
