//! Fixed-point linear interpolation by a step count (src/BATTLE/BATTLE.PRG/5BF94.c).

/// orig: vs_battle_lerpRatio (src/BATTLE/BATTLE.PRG/5BF94.c)
/// from + (to - from) * (step / steps), the ratio taken in 4.12 first; wrapping 32-bit arithmetic.
pub fn lerpRatio(from: c_int, to: c_int, step: c_int, steps: c_int) callconv(.c) c_int {
    const scaled = (to -% from) *% @divTrunc(step *% 4096, steps);
    return from +% @divTrunc(scaled, 4096);
}
