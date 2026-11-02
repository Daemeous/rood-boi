//! Eased interpolation between two values (src/BATTLE/BATTLE.PRG/5BF94.c).
const c = @import("c_5BF94");

/// orig: func_800CFC8C (src/BATTLE/BATTLE.PRG/5BF94.c)
/// from + (to - from) * (4096 - rcos(step * 2048 / steps)) / 8192.
pub fn func_800CFC8C(from: c_int, to: c_int, steps: c_int, step: c_int) callconv(.c) c_int {
    const weight = 4096 -% c.rcos(@divTrunc(@divTrunc(step *% 4096, 2), steps));
    return from +% @divTrunc((to -% from) *% weight, 4096 * 2);
}
