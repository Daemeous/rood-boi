//! Takes the pending-action flag vs_event_skipState and, when it was 2, runs vs_event_finishSkippedCutscene (pc/src/game/BATTLE/BATTLE.PRG/4A0A8.c).
const v = @import("views_4A0A8");
const c = @import("c_4A0A8");

/// orig: vs_event_scriptEndCutscene (pc/src/game/BATTLE/BATTLE.PRG/4A0A8.c)
/// Clears vs_event_skipState; when it was 2, runs vs_event_finishSkippedCutscene on arg0 and returns 4, else returns 0.
pub fn vs_event_scriptEndCutscene(arg0: [*c]u8, _: c_short) callconv(.c) c_int {
    const flag = v.g.vs_event_skipState();
    const temp_v1: u8 = flag.*;
    flag.* = 0;
    if (temp_v1 == 2) {
        c.vs_event_finishSkippedCutscene(arg0);
        return 4;
    }
    return 0;
}
