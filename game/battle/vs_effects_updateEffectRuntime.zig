//! Flips a battle flag, runs two battle steps around a display call and returns their OR (pc/src/game/BATTLE/BATTLE.PRG/5BF94.c).
const psx = @import("common").psx;
const v = @import("views_5BF94");
const c = @import("c_5BF94");

/// orig: vs_effects_updateEffectRuntime (pc/src/game/BATTLE/BATTLE.PRG/5BF94.c)
/// Toggles D_800EC2E4, runs two battle steps, and calls vs_effects_updateEffectCameraMotion when vs_effects_primaryPhase is 4.
pub fn vs_effects_updateEffectRuntime() callconv(.c) c_int {
    const flag = v.g.D_800EC2E4();
    flag.* = psx.conv(u8, @as(c_int, 1) -% @as(c_int, flag.*));
    c.vs_effects_selectEffectResourceContext(1);
    const temp_s1 = c.vs_effects_updatePrimaryEffects();
    c.vs_effects_selectEffectResourceContext(0);
    const temp_s0 = c.vs_effects_updateSecondaryEffects();
    c.vs_effects_selectEffectResourceContext(1);
    if (v.g.vs_effects_primaryPhase().* == 4) c.vs_effects_updateEffectCameraMotion();
    return temp_s1 | temp_s0;
}
