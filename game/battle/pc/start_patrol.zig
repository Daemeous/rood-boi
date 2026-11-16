//! Starting a waypoint patrol (D2: 72B70.s; oracle pc/src/battle_movement.c).
const ai = @import("views_71618");
const c = @import("c_battle_movement");

/// orig: vs_battle_aiPatrolWaypoints (pc/src/battle_movement.c)
/// Enters movement mode 5 and patrols its waypoints (vs_battle_aiUpdateWaypointPatrol). The
/// assembly skips both when $a2 is 0; the port always patrols (the oracle's
/// choice: its only caller passes no such argument).
pub fn startPatrol(state: *ai.vs_battle_aiState) callconv(.c) void {
    _ = c.vs_battle_aiSetMovementBehavior(state, 5);
    c.vs_battle_aiUpdateWaypointPatrol(state);
}
