//! Reads the battle byte vs_event_playbackPhase (pc/src/game/BATTLE/BATTLE.PRG/4A0A8.c).
const v = @import("views_4A0A8");

/// orig: vs_event_eventPlaybackState (pc/src/game/BATTLE/BATTLE.PRG/4A0A8.c)
/// Returns the value of vs_event_playbackPhase, widened to int as C promotes it.
pub fn vs_event_eventPlaybackState() callconv(.c) c_int {
    return @as(c_int, v.g.vs_event_playbackPhase().*);
}
