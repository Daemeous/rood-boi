//! Entering the engaged state (D2: 7399C.s; oracle pc/src/battle_movement.c).
const ai = @import("views_71618");
const c = @import("c_battle_movement");
const psx = @import("common").psx;

/// orig: vs_battle_aiTryBeginAshleyAlliance (pc/src/battle_movement.c)
/// Enters the engaged state (0x13) when forced, or on the second of two
/// `rand() & 0x7F < 0x4C` rolls (counted in 0x126): saves the movement
/// callback (0x68 -> 0x6C) and installs behaviour vs_battle_aiUpdateEngagementApproachBehavior.
pub fn enterEngaged(state: *ai.vs_battle_aiState, force: c_int) callconv(.c) void {
    if (force == 0) {
        if ((@as(c_uint, @bitCast(c.rand())) & 0x7f) >= 0x4c) return;
        state.unk126 +%= 1;
        if (state.unk126 < 2) return;
    }
    state.flag13 = 1;
    const saved = state.callback;
    state.callback = @ptrCast(@constCast(&c.behaviour_800DBC0C));
    state.savedCallback = saved;
    c.vs_battle_aiSetActorAwarenessIndicator(state.id, 0x80);
}
