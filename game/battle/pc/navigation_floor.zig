//! Navigation cell floor tests (D2: 7B5DC.s; oracle pc/src/battle_helpers.c).
const psx = @import("common").psx;
const c = @import("c_battle_helpers");

/// orig: func_800E4478 (pc/src/battle_helpers.c)
/// Whether height `y` is above the floor of the navigation cell at
/// (x >> 7, z >> 7) of the grid (64-byte rows) cached at scratchpad 0x3BC: a
/// cell with bit 13 set has floor 0, else floor = sign15(sign((cell & 0x3FF)
/// << 6) - ((cell & 0x1C00) >> 3)). Returns 1 when y < floor.
pub fn belowFloor(x: c_int, y: c_int, z: c_int) callconv(.c) c_int {
    const grid: [*]u8 = @ptrFromInt(@as(usize, @as(*u32, @ptrFromInt(0x1F8003BC)).*));
    const cell: c_uint = psx.byteAt(u16, grid, @as(isize, (z >> 7) *% 64) + @as(isize, (x >> 7) *% 2)).*;
    var floor: c_int = 0;
    if (cell & 0x2000 == 0) {
        const height: c_int = @as(c_int, @bitCast(cell << 22)) >> 16;
        floor = @as(c_int, @bitCast(@as(c_uint, @bitCast(height -% @as(c_int, @intCast((cell & 0x1c00) >> 3)))) << 17)) >> 17;
    }
    return @intFromBool(y < floor);
}

/// orig: func_800E44E8 (pc/src/battle_helpers.c)
/// Samples 16 points from `from` toward `to` (each Y lowered by its height
/// argument, X read as unsigned 16-bit) in steps of delta >> 4, and returns 1
/// as soon as a sample is not below the floor (func_800E4478 returns 0; Y
/// grows down), else 0.
pub fn pathClear(from: [*]const i16, to: [*]const i16, from_height: c_int, to_height: c_int) callconv(.c) c_int {
    var x: c_int = @as(u16, @bitCast(from[0]));
    var y: c_int = @as(c_int, from[1]) -% from_height;
    var z: c_int = from[2];
    const dx = (@as(c_int, @as(u16, @bitCast(to[0]))) -% x) >> 4;
    const dy = ((@as(c_int, to[1]) -% to_height) -% y) >> 4;
    const dz = (@as(c_int, to[2]) -% z) >> 4;
    var i: c_int = 0;
    while (i < 16) : (i += 1) { // 16 samples
        x +%= dx;
        y +%= dy;
        z +%= dz;
        if (c.func_800E4478(x, y, z) == 0) return 1;
    }
    return 0;
}
