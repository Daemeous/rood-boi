//! Sweeps the 32x32 tile grid and runs the per-tile check on each flagged tile
//! (pc/src/game/BATTLE/BATTLE.PRG/7CD68.c).
const v = @import("views_7CD68");
const c = @import("c_7CD68");

/// orig: vs_battle_aiBuildAiSpecialTerrainHeights (pc/src/game/BATTLE/BATTLE.PRG/7CD68.c)
/// Calls the per-tile check on every tile flagged 0x20 and not 0x2000 in the grid.
pub fn labelRegions() callconv(.c) void {
    var z: c_int = 0;
    while (z < 32) : (z +%= 1) {
        var x: c_int = 0;
        while (x < 32) : (x +%= 1) {
            // z and x are below 32 by the loop bounds: the casts cannot trap.
            const zi: usize = @intCast(z);
            const xi: usize = @intCast(x);
            if ((v.g.D_800F58B8().*.?[zi][xi] & 0x20) != 0 and (v.g.D_800F58D0().*.?[zi][xi] & 0x2000) == 0) {
                const r = c.vs_battle_aiFindAdjacentAiGroundHeight(x, z);
                if (r >= 0) {
                    c.vs_battle_aiFloodFillAiTerrainHeights(x, z, r);
                }
            }
        }
    }
}
