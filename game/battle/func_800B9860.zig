//! Battle helper from pc/src/game/BATTLE/BATTLE.PRG/4A0A8.c: forwards two bytes of its argument block to vs_field_swapRoomGeometryGroupFlags.
const c = @import("c_4A0A8");

/// orig: func_800B9860 (pc/src/game/BATTLE/BATTLE.PRG/4A0A8.c)
/// Calls vs_field_swapRoomGeometryGroupFlags with bytes 1 and 2 of its argument block and returns 0.
pub fn func_800B9860(arg0: [*c]u8, _: c_short) callconv(.c) c_int {
    _ = c.vs_field_swapRoomGeometryGroupFlags(@as(c_int, arg0[1]), @as(c_int, arg0[2]));
    return 0;
}
