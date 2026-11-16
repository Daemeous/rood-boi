//! Reads the battle value at vs_effects_trackCount when vs_effects_primaryPhase is 4 or 5, else 0 (pc/src/game/BATTLE/BATTLE.PRG/5BF94.c).
const v = @import("views_5BF94");

/// orig: vs_effects_playableTrackCount (pc/src/game/BATTLE/BATTLE.PRG/5BF94.c)
/// Returns vs_effects_trackCount while vs_effects_primaryPhase is 4 or 5 (unsigned range check), else 0.
pub fn vs_effects_playableTrackCount() callconv(.c) c_int {
    var result: c_int = 0;
    if ((v.g.vs_effects_primaryPhase().* -% 4) < 2) {
        result = v.g.vs_effects_trackCount().*;
    }
    return result;
}
