//! Battle overlay unit 6F014: copies one block pointer global into another (pc/src/game/BATTLE/BATTLE.PRG/6F014.c).
const v = @import("views_6F014");

/// orig: vs_effects_saveEffectResourceContext (pc/src/game/BATTLE/BATTLE.PRG/6F014.c)
/// Copies the pointer in vs_effects_package into vs_effects_savedPackage.
pub fn vs_effects_saveEffectResourceContext() callconv(.c) void {
    v.g.vs_effects_savedPackage().* = v.g.vs_effects_package().*;
}
