//! Battle unit 5BF94 wrapper that runs func_800D78F0 and reports success (src/BATTLE/BATTLE.PRG/5BF94.c).
const v = @import("views_5BF94");
const c = @import("c_5BF94");

/// orig: func_800D4D44 (src/BATTLE/BATTLE.PRG/5BF94.c)
/// Runs func_800D78F0 and returns 1; the argument is unused.
pub fn func_800D4D44(_: ?*v.D_800F53B8_t) callconv(.c) c_int {
    c.func_800D78F0();
    return 1;
}
