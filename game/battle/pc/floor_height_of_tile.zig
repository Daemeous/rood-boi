//! Floor height of a packed tile (D2: 7B5DC.s; oracle pc/src/battle_movement.c).
const c = @import("c_battle_movement");

/// orig: func_800E4308 (pc/src/battle_movement.c)
/// Raw floor height of a packed tile (x byte 0, z byte 2).
pub fn floorHeightOfTile(tile: c_uint) callconv(.c) c_int {
    return c.func_800E4320(@intCast(tile & 0xff), @intCast((tile >> 16) & 0xff)); // bytes
}
