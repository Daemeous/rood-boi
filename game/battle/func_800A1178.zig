//! Battle helper that looks up an entry and, when found, fills the output
//! through func_800A11D8 (src/BATTLE/BATTLE.PRG/30DB0.c).
const psx = @import("common").psx;
const v = @import("views_30DB0");
const c = @import("c_30DB0");

/// orig: func_800A1178 (src/BATTLE/BATTLE.PRG/30DB0.c)
/// Looks up arg0/arg1; a negative lookup result is returned, else fills the output and returns 0.
pub fn func_800A1178(arg0: c_int, arg1: c_int, arg2: ?*v.MATRIX, arg3: [*c]c_uint) callconv(.c) c_int {
    const temp_v0 = c.func_800A152C(arg0, arg1, 0);
    if (temp_v0 < 0) {
        return temp_v0;
    }
    const matrix: [*c]c.MATRIX = if (arg2) |m| psx.cptr(c.MATRIX, m) else null;
    c.func_800A11D8(arg0, temp_v0, matrix, arg3);
    return 0;
}
