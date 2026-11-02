//! Three calls in turn (src/BATTLE/BATTLE.PRG/146C.c).
const c = @import("c_146C");

/// orig: func_80073ACC (src/BATTLE/BATTLE.PRG/146C.c)
/// Calls func_800CB50C, func_8006C40C and func_8006F5CC.
pub fn func_80073ACC() callconv(.c) void {
    c.func_800CB50C();
    c.func_8006C40C();
    c.func_8006F5CC();
}
