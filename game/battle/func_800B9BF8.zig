//! Battle overlay wrapper: reports whether func_8007C398 returns nonzero (src/BATTLE/BATTLE.PRG/4A0A8.c).
const c = @import("c_4A0A8");

/// orig: func_800B9BF8 (src/BATTLE/BATTLE.PRG/4A0A8.c)
/// Returns 1 when the call to func_8007C398 is nonzero, else 0.
pub fn func_800B9BF8() callconv(.c) c_int {
    return @as(c_int, @intFromBool(c.func_8007C398() != 0));
}
