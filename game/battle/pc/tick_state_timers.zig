//! Per-actor byte timers (D2: 7B5DC.s; oracle pc/src/battle_helpers.c).
const ai = @import("views_71618");

/// orig: vs_battle_aiTickActorDecisionTimers (pc/src/battle_helpers.c)
/// Counts down the two byte timers of an actor update state (at 0x155 and
/// 0x13F), each stopping at zero.
pub fn tickStateTimers(state: *ai.vs_battle_aiState) callconv(.c) void {
    if (state.unk155 != 0) state.unk155 -= 1;
    if (state.minDelay != 0) state.minDelay -= 1;
}
