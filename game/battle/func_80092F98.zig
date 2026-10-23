//! Passes a word of the second argument block to func_80091998 (src/BATTLE/BATTLE.PRG/2842C.c).
const v = @import("views_2842C");
const c = @import("c_2842C");

/// orig: func_80092F98 (src/BATTLE/BATTLE.PRG/2842C.c)
/// Calls func_80091998 with the first word of the second argument's table and returns 0.
pub fn func_80092F98(_: ?*v.func_80092F74_t, arg1: ?*v.func_80092F74_t2) callconv(.c) c_int {
    _ = c.func_80091998(arg1.?.unk4[0].s32);
    return 0;
}
