//! Reads a field object's placement for the event scripts (pc/src/game/BATTLE/BATTLE.PRG/30DB0.c).
const v = @import("views_30DB0");

/// orig: vs_field_gimmickPlacement (pc/src/game/BATTLE/BATTLE.PRG/30DB0.c)
/// Always fails (-1) on the PC: the C first tests its object pointer before setting it ("BUG: Check performed
/// on garbage"), and GCC's object (both targets) folds the whole function to `return -1`.
pub fn vs_field_gimmickPlacement(index: c_int, out: ?*v.func_800A0024_t) callconv(.c) c_int {
    _ = index;
    _ = out;
    return -1;
}
