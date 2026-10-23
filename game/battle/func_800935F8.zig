//! Passes a stored word from its second argument to func_8009291C and returns 0
//! (src/BATTLE/BATTLE.PRG/2842C.c).
const v = @import("views_2842C");
const c = @import("c_2842C");

/// orig: func_800935F8 (src/BATTLE/BATTLE.PRG/2842C.c)
/// Forwards arg1's first unk4 word to func_8009291C; the first argument is unused.
pub fn func_800935F8(_: ?*v.func_80092F74_t, arg1: ?*v.func_80092F74_t2) callconv(.c) c_int {
    _ = c.func_8009291C(arg1.?.unk4[0].s32);
    return 0;
}
