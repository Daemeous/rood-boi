//! Forwards its argument to vs_effects_startEffectTrack (pc/src/game/BATTLE/BATTLE.PRG/146C.c).
const c = @import("c_146C");

/// orig: func_8007BEC4 (pc/src/game/BATTLE/BATTLE.PRG/146C.c)
/// Forwards `arg0` to vs_effects_startEffectTrack and discards its result.
pub fn func_8007BEC4(arg0: c_int) callconv(.c) void {
    _ = c.vs_effects_startEffectTrack(arg0);
}
