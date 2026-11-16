//! Stores an argument in battle global D_800F55E8 (pc/src/game/BATTLE/BATTLE.PRG/5BF94.c).
const psx = @import("common").psx;
const v = @import("views_5BF94");

/// orig: vs_effects_setEffectCameraTransitionMode (pc/src/game/BATTLE/BATTLE.PRG/5BF94.c)
/// Stores its argument in the global D_800F55E8.
pub fn vs_effects_setEffectCameraTransitionMode(arg0: c_int) callconv(.c) void {
    v.g.D_800F55E8().* = psx.conv(u32, arg0);
}
