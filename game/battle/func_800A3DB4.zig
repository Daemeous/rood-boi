//! Tests whether a point is a valid placement: its height clears the minimum and the flag bit is clear (src/BATTLE/BATTLE.PRG/3A1A0.c).
const std = @import("std");
const v = @import("views_3A1A0");
const c = @import("c_3A1A0");

/// orig: func_800A3DB4 (src/BATTLE/BATTLE.PRG/3A1A0.c)
/// Returns 1 when the point at (x, z) has a height of at least minimumHeight, its floor's bit 20 (D_800F49F0) is clear and no actor stands on its tile (func_800A3C34), else 0.
pub fn func_800A3DB4(x: c_int, z: c_int, minimumHeight: c_int) callconv(.c) c_int {
    var offset = std.mem.zeroes(c.SVECTOR);
    const height = c.func_800A6EE8(&offset, x, z, 1);
    if (height < minimumHeight) return 0;
    const flags = v.g.D_800F49F0().*.?[0];
    if (((flags >> 20) & 1) != 0) return 0;
    return @intFromBool(c.func_800A3C34(@divTrunc(x, 128), @divTrunc(z, 128), height, 1) == null);
}
