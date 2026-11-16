//! Returns the battle global vs_event_subroutineReturnCursor (pc/src/game/BATTLE/BATTLE.PRG/4A0A8.c).
const v = @import("views_4A0A8");

/// orig: vs_event_scriptReturnToSavedAddress (pc/src/game/BATTLE/BATTLE.PRG/4A0A8.c)
/// Returns the value of the global vs_event_subroutineReturnCursor; both arguments are unused.
pub fn vs_event_scriptReturnToSavedAddress(_: [*c]u8, _: c_short) callconv(.c) c_int {
    return v.g.vs_event_subroutineReturnCursor().*;
}
