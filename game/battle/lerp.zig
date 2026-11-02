//! Fixed-point linear interpolation (src/BATTLE/BATTLE.PRG/5BF94.c).

/// orig: vs_battle_lerp (src/BATTLE/BATTLE.PRG/5BF94.c)
/// from + (to - from) * ratio / 8192 (ratio 8192 = all the way), in wrapping 32-bit arithmetic.
pub fn lerp(from: c_int, to: c_int, ratio: c_int) callconv(.c) c_int {
    const scaled = (to -% from) *% ratio;
    return from +% @divTrunc(scaled, 4096 * 2);
}
