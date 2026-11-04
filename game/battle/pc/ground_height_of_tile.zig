//! Walkable ground height of a packed tile (D2: 7B5DC.s; oracle pc/src/battle_movement.c).
const c = @import("c_battle_movement");

/// orig: func_800E42E0 (pc/src/battle_movement.c)
/// Ground height of a packed tile (x byte 0, z byte 2).
pub fn groundHeightOfTile(tile: c_uint) callconv(.c) c_int {
    return c.func_800E42EC(@intCast(tile & 0xff), @intCast((tile >> 16) & 0xff)); // bytes
}
