//! Navigation grid cell flags (D2: 7BDD4.s; oracle pc/src/actor_cleanup.c).
const v = @import("views_actor_cleanup");

/// orig: func_800E4C28 (pc/src/actor_cleanup.c)
/// Clears bit 7 of the grid's cell (x, z): halfwords from D_800F58B8, 32 per
/// row; nothing without a grid. The offset is 32-bit unsigned arithmetic.
pub fn clearGridCellFlag(x: c_int, z: c_int) callconv(.c) void {
    const grid = v.g.D_800F58B8().*;
    if (grid != 0) {
        const cell: *align(1) u16 = @ptrFromInt(@as(usize, grid +% @as(c_uint, @bitCast(x)) *% 2 +% @as(c_uint, @bitCast(z)) *% 64));
        cell.* &= 0xff7f;
    }
}
