//! Forwards a battle list entry to func_800CFEF0 and reports success (src/BATTLE/BATTLE.PRG/5BF94.c).
const v = @import("views_5BF94");
const c = @import("c_5BF94");

/// orig: func_800D4A94 (src/BATTLE/BATTLE.PRG/5BF94.c)
/// Runs func_800CFEF0 on the given entry and returns 1.
pub fn func_800D4A94(arg0: ?*v.D_800F53B8_t) callconv(.c) c_int {
    c.func_800CFEF0(@ptrCast(arg0));
    return 1;
}
