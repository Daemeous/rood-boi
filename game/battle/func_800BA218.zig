//! Battle unit 4A0A8: forwards two shorts read from a packed argument block to vs_field_setRoomBackgroundOffset.
const c = @import("c_4A0A8");

/// orig: func_800BA218 (pc/src/game/BATTLE/BATTLE.PRG/4A0A8.c)
/// Reads the shorts at arg0+1 and arg0+3 and passes them to vs_field_setRoomBackgroundOffset; returns 0.
pub fn func_800BA218(arg0: [*c]u8, _: c_short) callconv(.c) c_int {
    // The object evaluates the arguments right to left: the read at arg0+3 happens first.
    const second: c_int = @as(c_int, c.vs_battle_getShort(arg0 + 3));
    const first: c_int = @as(c_int, c.vs_battle_getShort(arg0 + 1));
    c.vs_field_setRoomBackgroundOffset(first, second);
    return 0;
}
