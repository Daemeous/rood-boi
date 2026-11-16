//! Clears a battle global (pc/src/game/BATTLE/BATTLE.PRG/146C.c).
const v = @import("views_146C");

/// orig: vs_field_clearRoomTint (pc/src/game/BATTLE/BATTLE.PRG/146C.c)
/// Clears the battle global vs_field_isRoomTintActive.
pub fn vs_field_clearRoomTint() callconv(.c) void {
    v.g.vs_field_isRoomTintActive().* = 0;
}
