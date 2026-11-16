//! Choosing an actor's movement mode (D2: 70D4C.s; oracle pc/src/battle_helpers.c).
const ai = @import("views_71618");
const psx = @import("common").psx;
const c = @import("c_battle_helpers");

/// orig: vs_battle_aiSelectActionMovementMode (pc/src/battle_helpers.c)
/// Chooses the movement mode at 0x12E: mode 1 for an actor already running
/// vs_battle_aiUpdatePatrolCombatBehavior (its callback at 0x68) or whose best candidate (pointer at
/// 0x430) has no flag of its word 4 in the mask at 0x2D; otherwise mode 2 and
/// a waypoint search into 0x134.
pub fn chooseMovementMode(state: *ai.vs_battle_aiState) callconv(.c) void {
    const best: ?[*]align(1) c_uint = @ptrCast(state.entries.candidates[0]);
    if (@intFromPtr(state.callback) == @intFromPtr(&c.vs_battle_aiUpdatePatrolCombatBehavior) or best.?[4] & state.f2D == 0) {
        state.mode12E = 1;
        return;
    }
    state.mode12E = 2;
    c.vs_battle_aiFindNearestActiveWaypoint(state, &state.destination.bytes[0]);
}
