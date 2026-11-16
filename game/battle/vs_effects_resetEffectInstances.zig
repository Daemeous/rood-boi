//! Resets the battle state variables of unit 5BF94 (pc/src/game/BATTLE/BATTLE.PRG/5BF94.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_5BF94");
const c = @import("c_5BF94");

/// orig: vs_effects_resetEffectInstances (pc/src/game/BATTLE/BATTLE.PRG/5BF94.c)
/// Calls vs_effects_setPrimaryEffectPhase(0), then clears the battle state variables and the four-entry table vs_effects_tokenBitmaps.
pub fn vs_effects_resetEffectInstances() callconv(.c) void {
    c.vs_effects_setPrimaryEffectPhase(0);
    v.g.vs_effects_startedTrackCount().* = 0;
    v.g.vs_effects_trackCount().* = 0;
    v.g.vs_effects_primaryLoaded().* = 0;
    v.g.vs_effects_primaryToken().* = 0;
    psx.put(v.g.vs_effects_secondaryInstances(), null);
    psx.put(v.g.vs_effects_currentInstance(), null);
    v.g.D_800F53B4().* = 0;
    psx.put(v.g.vs_effects_primaryInstances(), null);
    v.g.vs_effects_curveSamples()[0] = 0;
    v.g.D_800F522C().* = 0;
    psx.put(v.g.D_800F5218(), null);
    var i: c_int = 0;
    while (i < 4) : (i +%= 1) {
        v.g.vs_effects_tokenBitmaps()[@as(usize, @intCast(i))] = 0;
    }
}
