//! Stub returning 0 for two unused arguments (src/BATTLE/BATTLE.PRG/2842C.c).
const v = @import("views_2842C");

/// orig: func_80092F6C (src/BATTLE/BATTLE.PRG/2842C.c)
/// Does nothing and returns 0; both arguments are unused.
pub fn func_80092F6C(_: ?*v.func_80092F74_t, _: ?*v.func_80092F74_t2) callconv(.c) c_int {
    return 0;
}
