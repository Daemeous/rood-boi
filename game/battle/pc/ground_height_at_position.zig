//! Walkable ground height at a position (D2: 7B5DC.s; oracle pc/src/battle_movement.c).
const c = @import("c_battle_movement");

/// orig: func_800E42D4 (pc/src/battle_movement.c)
/// Ground height of the tile containing world position (x, z) (128 units per tile).
pub fn groundHeightAtPosition(x: c_int, z: c_int) callconv(.c) c_int {
    return c.func_800E42EC(x >> 7, z >> 7);
}
