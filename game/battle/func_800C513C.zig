//! Calls func_8009FE74 with its arguments unless arg0 has a bit of 0xF0 set (src/BATTLE/BATTLE.PRG/5BF94.c).
const c = @import("c_5BF94");

/// orig: func_800C513C (src/BATTLE/BATTLE.PRG/5BF94.c)
/// Forwards to func_8009FE74 only when no bit of arg0's 0xF0 nibble group is set.
pub fn func_800C513C(arg0: c_int, arg1: c_int) callconv(.c) void {
    if ((arg0 & 0xF0) == 0) {
        _ = c.func_8009FE74(arg0, arg1);
    }
}
