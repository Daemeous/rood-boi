//! Calls vs_actors_enableModelPalettePulse unless any of bits 4-7 of arg0 are set (pc/src/game/BATTLE/BATTLE.PRG/5BF94.c).
const psx = @import("common").psx;
const c = @import("c_5BF94");

/// orig: func_800C5164 (pc/src/game/BATTLE/BATTLE.PRG/5BF94.c)
/// Forwards to vs_actors_enableModelPalettePulse when none of bits 4-7 of arg0 are set.
pub fn func_800C5164(arg0: c_int, arg1: c_int, arg2: c_int) callconv(.c) void {
    if ((arg0 & 0xF0) == 0) {
        _ = c.vs_actors_enableModelPalettePulse(arg0, arg1, arg2);
    }
}
