//! Clears a battle global in the 146C unit (pc/src/game/BATTLE/BATTLE.PRG/146C.c).
const v = @import("views_146C");

/// orig: vs_field_cancelPendingRoomVisit (pc/src/game/BATTLE/BATTLE.PRG/146C.c)
/// Sets vs_field_isRoomVisitPending to zero.
pub fn vs_field_cancelPendingRoomVisit() callconv(.c) void {
    v.g.vs_field_isRoomVisitPending().* = 0;
}
