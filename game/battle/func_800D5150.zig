//! Battle wrapper that forwards to func_800CE644 with argument 0x1E (pc/src/game/BATTLE/BATTLE.PRG/5BF94.c).
const v = @import("views_5BF94");
const c = @import("c_5BF94");

/// orig: func_800D5150 (pc/src/game/BATTLE/BATTLE.PRG/5BF94.c)
/// Calls func_800CE644(0x1E) and returns its result; the argument is unused.
pub fn func_800D5150(_: ?*v.vs_effects_EffectInstance) callconv(.c) c_int {
    return c.func_800CE644(0x1E);
}
