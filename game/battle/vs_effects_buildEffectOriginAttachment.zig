//! Forwards the vs_effects_effectOrigin table and the caller's argument to vs_core_mathTranslationTransform (pc/src/game/BATTLE/BATTLE.PRG/5BF94.c).
const v = @import("views_5BF94");
const c = @import("c_5BF94");

/// orig: vs_effects_buildEffectOriginAttachment (pc/src/game/BATTLE/BATTLE.PRG/5BF94.c)
/// Calls vs_core_mathTranslationTransform with the vs_effects_effectOrigin table and `arg0`.
pub fn vs_effects_buildEffectOriginAttachment(arg0: ?*v.func_800CFE98_t) callconv(.c) void {
    c.vs_core_mathTranslationTransform(@ptrCast(v.g.vs_effects_effectOrigin()), @ptrCast(arg0));
}
