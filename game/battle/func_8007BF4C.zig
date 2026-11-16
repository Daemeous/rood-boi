//! Battle unit 146C: forwards to vs_field_disableOwnedRoomRendering (pc/src/game/BATTLE/BATTLE.PRG/146C.c).
const c = @import("c_146C");

/// orig: func_8007BF4C (pc/src/game/BATTLE/BATTLE.PRG/146C.c)
/// Calls vs_field_disableOwnedRoomRendering with 0 and discards its result.
pub fn func_8007BF4C() callconv(.c) void {
    _ = c.vs_field_disableOwnedRoomRendering(0);
}
