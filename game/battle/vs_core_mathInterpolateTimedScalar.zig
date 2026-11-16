//! Linear interpolation between two values by step / steps (pc/src/game/BATTLE/BATTLE.PRG/5BF94.c).
const c = @import("c_5BF94");

/// orig: vs_core_mathInterpolateTimedScalar (pc/src/game/BATTLE/BATTLE.PRG/5BF94.c)
/// vs_battle_lerp from `from` to `to` by step * 8192 / steps.
pub fn vs_core_mathInterpolateTimedScalar(from: c_int, to: c_int, steps: c_int, step: c_int) callconv(.c) c_int {
    return c.vs_battle_lerp(from, to, @divTrunc(step *% (4096 * 2), steps));
}
