//! Forwards a call to func_800A07FC unless the low flag nibble of arg0 is set (src/BATTLE/BATTLE.PRG/5BF94.c).
const c = @import("c_5BF94");

/// orig: func_800C518C (src/BATTLE/BATTLE.PRG/5BF94.c)
/// Calls func_800A07FC with the same arguments when no bit of arg0's 0xF0 mask is set.
pub fn func_800C518C(arg0: c_int, arg1: ?*anyopaque) callconv(.c) void {
    if ((arg0 & 0xF0) == 0) {
        _ = c.func_800A07FC(arg0, @ptrCast(arg1));
    }
}
