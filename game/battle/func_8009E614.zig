//! Battle helper at 30DB0: forwards its argument to func_8009E700 with mode 6.
const c = @import("c_30DB0");

/// orig: func_8009E614 (src/BATTLE/BATTLE.PRG/30DB0.c)
/// Calls func_8009E700 with the given argument and mode 6.
pub fn func_8009E614(arg0: c_int) callconv(.c) void {
    c.func_8009E700(arg0, 6);
}
