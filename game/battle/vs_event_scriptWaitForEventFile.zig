//! Battle check on the global pointer vs_event_eventFileDiscRequest (pc/src/game/BATTLE/BATTLE.PRG/4A0A8.c).
const v = @import("views_4A0A8");

/// orig: vs_event_scriptWaitForEventFile (pc/src/game/BATTLE/BATTLE.PRG/4A0A8.c)
/// Returns 1 when vs_event_eventFileDiscRequest is set and the short it points at is not 4, else 0.
pub fn vs_event_scriptWaitForEventFile(_: [*c]u8, _: c_short) callconv(.c) c_int {
    const p = v.g.vs_event_eventFileDiscRequest().*;
    if (p != null and @as(c_int, p.?[0]) != 4) return 1;
    return 0;
}
