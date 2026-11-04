//! Wandering around a tile (D2: 72B70.s; oracle pc/src/battle_movement.c).
const c = @import("c_battle_movement");

/// orig: func_800DB5B8 (pc/src/battle_movement.c)
/// Enters movement mode 3 and wanders around (tile, range).
pub fn wanderAround(state: [*]u8, tile: c_uint, range: c_int) callconv(.c) void {
    _ = c.func_800DB4AC(state, 3);
    c.func_800E1EAC(state, tile, range);
}
