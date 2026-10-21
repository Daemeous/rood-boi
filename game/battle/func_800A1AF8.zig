//! Battle helper at 38C1C.c: forwards to func_800A1C10 and keeps only a negative result.
const c = @import("c_38C1C");

/// orig: func_800A1AF8 (src/BATTLE/BATTLE.PRG/38C1C.c)
/// Calls func_800A1C10 with the same arguments and returns its result if negative, else 0.
pub fn func_800A1AF8(arg0: c_int, arg1: c_int, arg2: [*c]c_ushort, arg3: c_int) callconv(.c) c_int {
    const result = c.func_800A1C10(arg0, arg1, arg2, arg3);

    if (result < 0) {
        return result;
    }

    return 0;
}
