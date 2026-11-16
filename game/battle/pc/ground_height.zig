//! Walkable ground height of a tile (D2: 7B5DC.s; oracle pc/src/battle_movement.c).
const c = @import("c_battle_movement");

/// orig: vs_field_groundHeightAtTile (pc/src/battle_movement.c)
/// Floor height of tile (x, z) sign-extended from 15 bits.
pub fn groundHeight(x: c_int, z: c_int) callconv(.c) c_int {
    const raw: c_uint = @bitCast(c.vs_field_groundHeightFlagsAtTile(x, z));
    const shifted: c_int = @bitCast(raw << 17);
    return shifted >> 17;
}
