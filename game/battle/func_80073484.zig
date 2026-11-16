//! Battle frame setup: calls the two engine helpers and sets the game time tick speed (pc/src/game/BATTLE/BATTLE.PRG/146C.c).
const v = @import("views_146C");
const c = @import("c_146C");

/// orig: func_80073484 (pc/src/game/BATTLE/BATTLE.PRG/146C.c)
/// Runs the two setup calls around setting `vs_gametime_tickspeed` to 2.
pub fn func_80073484() callconv(.c) void {
    c.func_8006F5CC();
    v.g.vs_gametime_tickspeed().* = 2;
    c.vs_battle_setHudHidden(0);
    _ = c.vs_field_setMinimapVisible(1);
}
