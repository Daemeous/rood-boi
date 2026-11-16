//! Floor height of a packed tile (D2: 7B5DC.s; oracle pc/src/battle_movement.c).
const c = @import("c_battle_movement");

/// orig: vs_field_groundHeightFlagsAtPackedTile (pc/src/battle_movement.c)
/// Raw floor height of a packed tile (x byte 0, z byte 2).
pub fn floorHeightOfTile(tile: c_uint) callconv(.c) c_int {
    return c.vs_field_groundHeightFlagsAtTile(@intCast(tile & 0xff), @intCast((tile >> 16) & 0xff)); // bytes
}
