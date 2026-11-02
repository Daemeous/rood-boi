//! Battle unit 4A0A8: a small forwarder (src/BATTLE/BATTLE.PRG/4A0A8.c).
const c = @import("c_4A0A8");

/// orig: func_800B9818 (src/BATTLE/BATTLE.PRG/4A0A8.c)
/// Passes the byte at arg0[1] to func_80091998 and returns 0.
pub fn func_800B9818(arg0: [*c]u8, _: c_short) callconv(.c) c_int {
    _ = c.func_80091998(@as(c_int, arg0[1]));
    return 0;
}
