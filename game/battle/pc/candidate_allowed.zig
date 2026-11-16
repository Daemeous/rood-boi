//! Whether a candidate is allowed now (D2: 73460.s; oracle pc/src/battle_movement.c).
const ai = @import("views_71618");
const c = @import("c_battle_movement");
const psx = @import("common").psx;

/// orig: vs_battle_aiIsActionCooldownReady (pc/src/battle_movement.c)
/// Candidate allowed now: not while the halfword at 0x90 is set, and not when
/// vs_battle_aiDoesActionRequireCooldown holds and the timer at 0x13F is running.
pub fn candidateAllowed(state: *ai.vs_battle_aiState, candidate: [*]u8) callconv(.c) c_int {
    if (state.unk90 != 0) return 0;
    if (c.vs_battle_aiDoesActionRequireCooldown(state, candidate) == 0) return 1;
    return @intFromBool(state.minDelay == 0);
}
