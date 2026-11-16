//! Battle callback that runs vs_effects_updateEffectTransforms on a window record (pc/src/game/BATTLE/BATTLE.PRG/5BF94.c).
const v = @import("views_5BF94");
const c = @import("c_5BF94");

/// orig: vs_effects_tickScriptTransforms (pc/src/game/BATTLE/BATTLE.PRG/5BF94.c)
/// Runs vs_effects_updateEffectTransforms on the record and reports success.
pub fn vs_effects_tickScriptTransforms(arg0: ?*v.D_800F53B8_t) callconv(.c) c_int {
    c.vs_effects_updateEffectTransforms(@ptrCast(arg0));
    return 1;
}
