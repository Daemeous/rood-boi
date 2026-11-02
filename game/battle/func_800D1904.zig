//! Linear interpolation between two values by step / steps (src/BATTLE/BATTLE.PRG/5BF94.c).
const c = @import("c_5BF94");

/// orig: func_800D1904 (src/BATTLE/BATTLE.PRG/5BF94.c)
/// vs_battle_lerp from `from` to `to` by step * 8192 / steps.
pub fn func_800D1904(from: c_int, to: c_int, steps: c_int, step: c_int) callconv(.c) c_int {
    return c.vs_battle_lerp(from, to, @divTrunc(step *% (4096 * 2), steps));
}
