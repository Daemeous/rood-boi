//! Calls vs_actors_disableModelPalettePulse with its arguments unless arg0 has a bit of 0xF0 set (pc/src/game/BATTLE/BATTLE.PRG/5BF94.c).
const c = @import("c_5BF94");

/// orig: func_800C513C (pc/src/game/BATTLE/BATTLE.PRG/5BF94.c)
/// Forwards to vs_actors_disableModelPalettePulse only when no bit of arg0's 0xF0 nibble group is set.
pub fn func_800C513C(arg0: c_int, arg1: c_int) callconv(.c) void {
    if ((arg0 & 0xF0) == 0) {
        _ = c.vs_actors_disableModelPalettePulse(arg0, arg1);
    }
}
