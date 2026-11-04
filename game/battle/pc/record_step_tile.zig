//! Step tile toward the target (D2: 7B3C8.s; oracle pc/src/battle_movement.c).
const psx = @import("common").psx;

/// orig: func_800E3BC8 (pc/src/battle_movement.c)
/// Records the step tile toward the target (0x168) in the path flags (0x190
/// bits 22+ = x, 27+ = z; 0 per axis already reached, otherwise the target
/// coordinate plus one when it lies ahead). The fields overlap, as in the original.
pub fn recordStepTile(state: [*]u8) callconv(.c) void {
    const target = @as(*align(1) u32, @ptrCast(state + 0x168)).*;
    const tile = @as(*align(1) u32, @ptrCast(state + 0x34)).*;
    const target_x: c_int = @intCast(target & 0xff); // a byte
    const target_z: c_int = @intCast((target >> 16) & 0xff);
    const tile_x: c_int = @intCast(tile & 0xff);
    const tile_z: c_int = @intCast((tile >> 16) & 0xff);
    const x: c_uint = if (target_x != tile_x) psx.conv(c_uint, target_x +% @intFromBool(tile_x < target_x)) else 0;
    const z: c_uint = if (target_z != tile_z) psx.conv(c_uint, target_z +% @intFromBool(tile_z < target_z)) else 0;
    const flags = @as(*align(1) u32, @ptrCast(state + 0x190));
    flags.* = (flags.* & 0x3fffff) | (x << 22) | (z << 27);
}
