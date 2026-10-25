//! Passes a guest pointer through to func_800A141C and reports only its failures (src/BATTLE/BATTLE.PRG/30DB0.c).
const psx = @import("common").psx;
const c = @import("c_30DB0");

/// orig: func_800A13EC (src/BATTLE/BATTLE.PRG/30DB0.c)
/// Calls func_800A141C with arg2 as a guest address; returns its result if negative, else 0.
pub fn func_800A13EC(arg0: c_int, arg1: c_int, arg2: [*c]c_int, arg3: c_int) callconv(.c) c_int {
    const result = c.func_800A141C(arg0, arg1, @bitCast(psx.addr(arg2)), arg3);
    if (result < 0) {
        return result;
    }
    return 0;
}
