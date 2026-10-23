//! Battle unit 2842C: forwards a word from the message block to func_800919D8 (src/BATTLE/BATTLE.PRG/2842C.c).
const v = @import("views_2842C");
const c = @import("c_2842C");

/// orig: func_8009361C (src/BATTLE/BATTLE.PRG/2842C.c)
/// Passes the first word of the block's message entry to func_800919D8 and returns 0.
pub fn func_8009361C(_: ?*v.func_80092F74_t, arg1: ?*v.func_80092F74_t2) callconv(.c) c_int {
    _ = c.func_800919D8(arg1.?.unk4[0].s32);
    return 0;
}
