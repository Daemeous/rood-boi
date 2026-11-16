//! Calls the battle helper vs_field_hideRoomGeometryGroup with the byte at arg0[1]
//! (pc/src/game/BATTLE/BATTLE.PRG/4A0A8.c).
const c = @import("c_4A0A8");

/// orig: func_800B983C (pc/src/game/BATTLE/BATTLE.PRG/4A0A8.c)
/// Passes the byte at arg0[1] to vs_field_hideRoomGeometryGroup and returns 0.
pub fn func_800B983C(arg0: [*c]u8, _: c_short) callconv(.c) c_int {
    _ = c.vs_field_hideRoomGeometryGroup(@as(c_int, arg0[1]));
    return 0;
}
