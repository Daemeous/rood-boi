//! Battle flag copy (pc/src/game/BATTLE/BATTLE.PRG/4A0A8.c).
const v = @import("views_4A0A8");

/// orig: vs_event_requestEventStart (pc/src/game/BATTLE/BATTLE.PRG/4A0A8.c)
/// Copies the flag at vs_event_playbackPhase to vs_event_isStartConfirmed when it is 1, and returns 1.
pub fn vs_event_requestEventStart() callconv(.c) c_int {
    if (@as(c_int, v.g.vs_event_playbackPhase().*) == 1) {
        v.g.vs_event_isStartConfirmed().* = v.g.vs_event_playbackPhase().*;
        return 1;
    }
    return 1;
}
