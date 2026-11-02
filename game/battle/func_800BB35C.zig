//! Battle unit 4A0A8: passes bits 0 and 1 of byte 1 of a block to func_800BE36C.
const c = @import("c_4A0A8");

/// orig: func_800BB35C (src/BATTLE/BATTLE.PRG/4A0A8.c)
/// Calls func_800BE36C with bit 0 of arg0[1], and whether bit 1 of arg0[1] is set.
pub fn func_800BB35C(arg0: [*c]u8, _: c_short) callconv(.c) c_int {
    const flags: c_int = @as(c_int, arg0[1]);
    c.func_800BE36C(flags & 1, @intFromBool((flags & 2) != 0));
    return 0;
}
