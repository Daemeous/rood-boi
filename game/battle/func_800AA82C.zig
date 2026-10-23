//! Forwards to func_800AA984 (src/BATTLE/BATTLE.PRG/40564.c).
const c = @import("c_40564");

/// orig: func_800AA82C (src/BATTLE/BATTLE.PRG/40564.c)
/// Passes its three arguments straight on to func_800AA984.
pub fn func_800AA82C(arg0: c_int, arg1: c_short, arg2: c_int) callconv(.c) void {
    c.func_800AA984(arg0, arg1, arg2);
}
