//! Battle unit 4A0A8: forwards byte 1 of its argument to func_800CEF38
//! (src/BATTLE/BATTLE.PRG/4A0A8.c).
const c = @import("c_4A0A8");

/// orig: func_800BAACC (src/BATTLE/BATTLE.PRG/4A0A8.c)
/// Passes byte 1 of `arg0` to func_800CEF38 and returns 0; `arg1` is unused.
pub fn func_800BAACC(arg0: [*c]u8, arg1: c_short) callconv(.c) c_int {
    _ = arg1;
    c.func_800CEF38(@as(c_int, arg0[1]));
    return 0;
}
