//! Two-step battle call sequence that stops at the first negative result (src/BATTLE/BATTLE.PRG/38C1C.c).
const c = @import("c_38C1C");

/// orig: func_800A1B9C (src/BATTLE/BATTLE.PRG/38C1C.c)
/// Runs func_800A152C then func_800A1C10, returning the first negative result, else 0.
pub fn func_800A1B9C(arg0: c_int, arg1: c_int, arg2: [*c]c_ushort, arg3: c_int) callconv(.c) c_int {
    var result: c_int = c.func_800A152C(arg0, arg1, 2);

    if (result < 0) {
        return result;
    }

    result = c.func_800A1C10(arg0, result, arg2, arg3);

    if (result < 0) {
        return result;
    }

    return 0;
}
