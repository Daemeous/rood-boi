//! Clears the battle flag at D_800F522C (pc/src/game/BATTLE/BATTLE.PRG/5BF94.c).
const v = @import("views_5BF94");

/// orig: func_800D4D98 (pc/src/game/BATTLE/BATTLE.PRG/5BF94.c)
/// Clears D_800F522C and reports success.
pub fn func_800D4D98(_: ?*v.vs_effects_EffectInstance) callconv(.c) c_int {
    v.g.D_800F522C().* = 0;
    return 1;
}
