//! Step tile toward the target (D2: 7B3C8.s; oracle pc/src/battle_movement.c).
const ai = @import("views_71618");
const psx = @import("common").psx;

/// orig: vs_battle_aiSetDetourBoundary (pc/src/battle_movement.c)
/// Records the step tile toward the target (0x168) in the path flags (0x190
/// bits 22+ = x, 27+ = z; 0 per axis already reached, otherwise the target
/// coordinate plus one when it lies ahead). The fields overlap, as in the original.
pub fn recordStepTile(state: *ai.vs_battle_aiState) callconv(.c) void {
    const target = state.destination168.destination;
    const tile = state.tile.raw;
    const target_x: c_int = @intCast(target & 0xff); // a byte
    const target_z: c_int = @intCast((target >> 16) & 0xff);
    const tile_x: c_int = @intCast(tile & 0xff);
    const tile_z: c_int = @intCast((tile >> 16) & 0xff);
    const x: c_uint = if (target_x != tile_x) psx.conv(c_uint, target_x +% @intFromBool(tile_x < target_x)) else 0;
    const z: c_uint = if (target_z != tile_z) psx.conv(c_uint, target_z +% @intFromBool(tile_z < target_z)) else 0;
    const flags = &state.movementFlags.raw;
    flags.* = (flags.* & 0x3fffff) | (x << 22) | (z << 27);
}
