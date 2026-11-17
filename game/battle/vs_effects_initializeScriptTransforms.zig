//! Forwards a battle list entry to vs_effects_initializeTransformStates and reports success (pc/src/game/BATTLE/BATTLE.PRG/5BF94.c).
const v = @import("views_5BF94");
const c = @import("c_5BF94");

/// orig: vs_effects_initializeScriptTransforms (pc/src/game/BATTLE/BATTLE.PRG/5BF94.c)
/// Runs vs_effects_initializeTransformStates on the given entry and returns 1.
pub fn vs_effects_initializeScriptTransforms(arg0: ?*v.vs_effects_EffectInstance) callconv(.c) c_int {
    c.vs_effects_initializeTransformStates(@ptrCast(arg0));
    return 1;
}
