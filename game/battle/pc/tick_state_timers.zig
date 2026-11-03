//! Per-actor byte timers (D2: 7B5DC.s; oracle pc/src/battle_helpers.c).

/// orig: func_800E4358 (pc/src/battle_helpers.c)
/// Counts down the two byte timers of an actor update state (at 0x155 and
/// 0x13F), each stopping at zero.
pub fn tickStateTimers(state: [*]u8) callconv(.c) void {
    if (state[0x155] != 0) state[0x155] -= 1;
    if (state[0x13f] != 0) state[0x13f] -= 1;
}
