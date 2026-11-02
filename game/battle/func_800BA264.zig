//! Battle unit 4A0A8: forwards whether byte 1 of its argument is set to func_8007BDD8.
const c = @import("c_4A0A8");

/// orig: func_800BA264 (src/BATTLE/BATTLE.PRG/4A0A8.c)
/// Passes whether byte 1 of its argument is nonzero to func_8007BDD8; returns 0.
pub fn func_800BA264(arg0: [*c]u8, _: c_short) callconv(.c) c_int {
    _ = c.func_8007BDD8(@intFromBool(arg0[1] != 0));
    return 0;
}
