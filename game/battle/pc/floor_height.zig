//! Floor height of a tile (D2: 7B5DC.s; oracle pc/src/battle_movement.c).
const psx = @import("common").psx;

/// orig: func_800E4320 (pc/src/battle_movement.c)
/// Raw floor height of tile (x, z) from the grid cached at scratchpad 0x3BC
/// (64-byte rows of 16-bit cells): sign((cell & 0x3FF) << 6) minus
/// (cell & 0x1C00) >> 3.
pub fn floorHeight(x: c_int, z: c_int) callconv(.c) c_int {
    const grid: usize = @as(*const u32, @ptrFromInt(0x1f8003bc)).*;
    const offset: isize = @as(isize, z *% 64) +% @as(isize, x *% 2);
    const cell: c_uint = @as(*align(1) const u16, @ptrFromInt(grid +% @as(usize, @bitCast(offset)))).*;
    const shifted: c_int = @bitCast(cell << 22);
    return (shifted >> 16) -% @as(c_int, @bitCast((cell & 0x1c00) >> 3));
}
