//! Reads a field object's placement for the event scripts (src/BATTLE/BATTLE.PRG/30DB0.c).
const v = @import("views_30DB0");

/// orig: func_800A0024 (src/BATTLE/BATTLE.PRG/30DB0.c)
/// Always fails (-1) on the PC: the C first tests its object pointer before setting it ("BUG: Check performed
/// on garbage"), and GCC's object (both targets) folds the whole function to `return -1`.
pub fn func_800A0024(index: c_int, out: ?*v.func_800A0024_t) callconv(.c) c_int {
    _ = index;
    _ = out;
    return -1;
}
