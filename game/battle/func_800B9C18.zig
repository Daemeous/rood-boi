//! Battle unit 4A0A8: branches on a byte of its argument to one of two calls.
const c = @import("c_4A0A8");

/// orig: func_800B9C18 (src/BATTLE/BATTLE.PRG/4A0A8.c)
/// Calls one of two battle helpers, chosen by the byte at arg0[1]; always returns 0.
pub fn func_800B9C18(arg0: [*c]u8, _: c_short) callconv(.c) c_int {
    if (arg0[1] != 0) {
        c.func_8007B104();
    } else {
        c.func_8007B0FC();
    }
    return 0;
}
