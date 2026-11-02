//! Battle script opcode: passes the door byte of a script to setDoorEntered2
//! (src/BATTLE/BATTLE.PRG/4A0A8.c).
const c = @import("c_4A0A8");

/// orig: func_800BAC38 (src/BATTLE/BATTLE.PRG/4A0A8.c)
/// Passes the script's second byte to `vs_battle_setDoorEntered2` and returns 0.
pub fn func_800BAC38(arg0: [*c]u8, _: c_short) callconv(.c) c_int {
    c.vs_battle_setDoorEntered2(@as(c_int, arg0[1]));
    return 0;
}
