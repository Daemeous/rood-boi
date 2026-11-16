//! Wandering around a tile (D2: 72B70.s; oracle pc/src/battle_movement.c).
const ai = @import("views_71618");
const c = @import("c_battle_movement");

/// orig: vs_battle_aiWanderAroundTile (pc/src/battle_movement.c)
/// Enters movement mode 3 and wanders around (tile, range).
pub fn wanderAround(state: *ai.vs_battle_aiState, tile: c_uint, range: c_int) callconv(.c) void {
    _ = c.vs_battle_aiSetMovementBehavior(state, 3);
    c.vs_battle_aiUpdateWanderMovement(state, tile, range);
}
