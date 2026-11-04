//! Navigation cell occupied mark (D2: 7BDD4.s; oracle pc/src/battle_movement.c).
const psx = @import("common").psx;
const v = @import("views_battle_movement");

/// orig: func_800E4C1C (pc/src/battle_movement.c)
/// Marks navigation cell (x, z) of D_800F58B8 occupied (bit 0x80).
pub fn markCellOccupied(x: c_int, z: c_int) callconv(.c) void {
    const grid = v.g.D_800F58B8().*;
    if (grid == 0) return;
    const cell: *align(1) u16 = @ptrFromInt(grid +% psx.conv(c_uint, x *% 2) +% psx.conv(c_uint, z *% 64));
    cell.* |= 0x80;
}
