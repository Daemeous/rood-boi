//! Battle stub that ignores its argument and returns 2 (pc/src/game/BATTLE/BATTLE.PRG/5BF94.c).
const v = @import("views_5BF94");

/// orig: vs_effects_endScript (pc/src/game/BATTLE/BATTLE.PRG/5BF94.c)
/// Ignores its argument and returns 2.
pub fn vs_effects_endScript(_: ?*v.D_800F53B8_t) callconv(.c) c_int {
    return 2;
}
