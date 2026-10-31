//! Battle callback that runs func_800D0D08 on a window record (src/BATTLE/BATTLE.PRG/5BF94.c).
const v = @import("views_5BF94");
const c = @import("c_5BF94");

/// orig: func_800D4984 (src/BATTLE/BATTLE.PRG/5BF94.c)
/// Runs func_800D0D08 on the record and reports success.
pub fn func_800D4984(arg0: ?*v.D_800F53B8_t) callconv(.c) c_int {
    c.func_800D0D08(@ptrCast(arg0));
    return 1;
}
