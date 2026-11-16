//! Battle unit 146C: a one-call wrapper around vs_field_enableOwnedRoomRendering (pc/src/game/BATTLE/BATTLE.PRG/146C.c).
const c = @import("c_146C");

/// orig: func_8007BF2C (pc/src/game/BATTLE/BATTLE.PRG/146C.c)
/// Calls vs_field_enableOwnedRoomRendering with 0 and discards its result.
pub fn func_8007BF2C() callconv(.c) void {
    _ = c.vs_field_enableOwnedRoomRendering(0);
}
