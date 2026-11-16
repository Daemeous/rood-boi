//! Copies the battle pointer vs_effects_savedPackage into vs_effects_package (pc/src/game/BATTLE/BATTLE.PRG/6F014.c).
const v = @import("views_6F014");

/// orig: vs_effects_restoreEffectResourceContext (pc/src/game/BATTLE/BATTLE.PRG/6F014.c)
/// Copies the pointer held in vs_effects_savedPackage into vs_effects_package.
pub fn vs_effects_restoreEffectResourceContext() callconv(.c) void {
    v.g.vs_effects_package().* = v.g.vs_effects_savedPackage().*;
}
