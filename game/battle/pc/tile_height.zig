//! Navigation tile height (D2: 7E05C.s; oracle pc/src/navigation.c).
const c = @import("c_navigation");

/// orig: func_800E71DC (pc/src/navigation.c)
/// The height of tile (x, z): the highest of func_8008DC7C's 4x4 samples over
/// the tile (128 units a side, 32 apart) and the sample at its middle (+64,+64),
/// compared as signed 15-bit heights with the high flags kept in the result. A
/// negative sample marks its grid square blocked; with none blocked, or any
/// 2x2 block of samples (squares 3 and 7 skipped) open, the highest is
/// returned, otherwise the height of sample 10.
pub fn tileHeight(x: c_int, z: c_int) callconv(.c) c_int {
    const x0 = x *% 128;
    const z0 = z *% 128;
    var highest = c.func_8008DC7C(x0 +% 64, z0 +% 64);
    var center: c_int = 0;
    var blocked: c_uint = 0;
    var i: u32 = 0;
    while (i < 16) : (i += 1) {
        const height = c.func_8008DC7C(x0 +% @as(c_int, @intCast((i >> 2) * 32)), z0 +% @as(c_int, @intCast((i & 3) * 32)));
        if (i == 10) center = height;
        if (height < 0) blocked |= @as(c_uint, 1) << @as(u5, @intCast(i));
        // Signed 15-bit heights, the separate high flags ignored.
        const candidate = @as(c_int, @bitCast(@as(c_uint, @bitCast(height)) << 17)) >> 17;
        const current = @as(c_int, @bitCast(@as(c_uint, @bitCast(highest)) << 17)) >> 17;
        if (current < candidate) highest = height;
    }
    if (blocked == 0) return highest;
    const open = ~blocked;
    var j: u32 = 0;
    while (j < 10) : (j += 1) {
        if (j == 3 or j == 7) continue;
        const square = @as(c_uint, 0x33) << @as(u5, @intCast(j));
        if (open & square == square) return highest;
    }
    return center;
}
