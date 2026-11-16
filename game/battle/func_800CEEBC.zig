//! Checks a battle state window and reports whether a counter is nonzero
//! (pc/src/game/BATTLE/BATTLE.PRG/5BF94.c).
const v = @import("views_5BF94");

/// orig: func_800CEEBC (pc/src/game/BATTLE/BATTLE.PRG/5BF94.c)
/// Returns 1 when vs_effects_primaryPhase - 4 is below 2 and vs_effects_trackCount > vs_effects_startedTrackCount and vs_effects_pendingTracks is nonzero, else 0.
pub fn func_800CEEBC() callconv(.c) c_int {
    var var_a1: c_int = 0;
    if (((v.g.vs_effects_primaryPhase().* -% 4) < 2) and (v.g.vs_effects_trackCount().* > v.g.vs_effects_startedTrackCount().*)) {
        var_a1 = @intFromBool(v.g.vs_effects_pendingTracks().* > 0);
    }
    return var_a1;
}
