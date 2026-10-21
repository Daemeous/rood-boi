//! Battle unit 38C1C: chains two calls and returns the first negative status, else 0.
const c = @import("c_38C1C");

/// orig: func_800A1B28 (src/BATTLE/BATTLE.PRG/38C1C.c)
/// Runs func_800A152C, then func_800A1C10 on its result; returns the first negative status, else 0.
pub fn func_800A1B28(arg0: c_int, arg1: c_int, arg2: [*c]c_ushort, arg3: c_int) callconv(.c) c_int {
    var var_v0: c_int = c.func_800A152C(arg0, arg1, 1);

    if (var_v0 < 0) {
        return var_v0;
    }

    var_v0 = c.func_800A1C10(arg0, var_v0, arg2, arg3);

    if (var_v0 < 0) {
        return var_v0;
    }

    return 0;
}
