//! Runs two battle steps in order (pc/src/game/BATTLE/BATTLE.PRG/146C.c).
const c = @import("c_146C");

/// orig: vs_event_startPreparedCutscene (pc/src/game/BATTLE/BATTLE.PRG/146C.c)
/// Calls vs_event_requestEventStart (its result is discarded), then vs_event_prepareCutsceneActors.
pub fn vs_event_startPreparedCutscene() callconv(.c) void {
    _ = c.vs_event_requestEventStart();
    c.vs_event_prepareCutsceneActors();
}
