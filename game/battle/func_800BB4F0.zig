//! Stores byte 1 of its buffer argument in D_800F4BE4 (src/BATTLE/BATTLE.PRG/4A0A8.c).
const v = @import("views_4A0A8");

/// orig: func_800BB4F0 (src/BATTLE/BATTLE.PRG/4A0A8.c)
/// Copies byte 1 of `arg0` into D_800F4BE4 and returns 0; the second argument is unused.
pub fn func_800BB4F0(arg0: [*c]u8, _: c_short) callconv(.c) c_int {
    v.g.D_800F4BE4().* = arg0[1];
    return 0;
}
