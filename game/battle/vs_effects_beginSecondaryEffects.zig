//! Clears the two camera views and runs the battle camera setup (pc/src/game/BATTLE/BATTLE.PRG/5BF94.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_5BF94");
const c = @import("c_5BF94");

/// orig: vs_effects_beginSecondaryEffects (pc/src/game/BATTLE/BATTLE.PRG/5BF94.c)
/// Resets the camera state, then runs the setup for `effect` with `arg1`, returning its result.
pub fn vs_effects_beginSecondaryEffects(arg0: ?*v.func_800CF0E8_t, arg1: c_int) callconv(.c) c_int {
    c.vs_effects_selectEffectResourceContext(0);
    c.vs_effects_captureEffectCamera(psx.cptr(c.D_800F54D8_t, v.g.vs_effects_savedActionCamera()));
    c.vs_effects_captureEffectCamera(psx.cptr(c.D_800F54D8_t, v.g.vs_effects_effectCamera()));
    return @as(c_int, c.func_800CF218(@ptrCast(arg0), 0, arg1));
}
