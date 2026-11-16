//! Floor height at a position (D2: 7B5DC.s; oracle pc/src/battle_movement.c).
const c = @import("c_battle_movement");

/// orig: vs_field_groundHeightFlagsAtWorldPosition (pc/src/battle_movement.c)
/// Raw floor height of the tile containing world position (x, z) (128 units per tile).
pub fn floorHeightAtPosition(x: c_int, z: c_int) callconv(.c) c_int {
    return c.vs_field_groundHeightFlagsAtTile(x >> 7, z >> 7);
}
