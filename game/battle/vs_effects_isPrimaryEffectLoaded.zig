//! Battle flag query: reports whether vs_effects_primaryLoaded is set (pc/src/game/BATTLE/BATTLE.PRG/5BF94.c).
const v = @import("views_5BF94");

/// orig: vs_effects_isPrimaryEffectLoaded (pc/src/game/BATTLE/BATTLE.PRG/5BF94.c)
/// Returns 1 when the battle flag vs_effects_primaryLoaded is nonzero, else 0.
pub fn vs_effects_isPrimaryEffectLoaded() callconv(.c) c_int {
    return @intFromBool(v.g.vs_effects_primaryLoaded().* != 0);
}
