//! Stores its argument in the global vs_effects_primaryPhase (pc/src/game/BATTLE/BATTLE.PRG/5BF94.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_5BF94");

/// orig: vs_effects_setPrimaryEffectPhase (pc/src/game/BATTLE/BATTLE.PRG/5BF94.c)
/// Sets the global vs_effects_primaryPhase to `arg0`.
pub fn vs_effects_setPrimaryEffectPhase(arg0: c_int) callconv(.c) void {
    v.g.vs_effects_primaryPhase().* = psx.conv(u32, arg0);
}
