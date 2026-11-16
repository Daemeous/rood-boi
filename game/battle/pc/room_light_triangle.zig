//! Room lighting at a point inside one triangle (D2: 2EA3C.s; oracle pc/src/room_lighting.c).
const c = @import("c_room_lighting");
const gte = @import("common").gte;


/// orig: light_area (pc/src/room_lighting.c)
/// Signed area of the screen triangle (a, b, third): MAC0 of GTE NCLIP.
fn lightArea(a: c_uint, b: c_uint, third: c_uint) c_int {
    var ctx = gte.Ctx.init(); // this call's own registers
    ctx.mtc2(12, a);
    ctx.mtc2(13, b);
    ctx.mtc2(14, third);
    ctx.cop2(0x01400006); // gte_nclip
    return @bitCast(ctx.mfc2(24));
}

/// orig: vs_render_interpolateRoomLightTriangle (pc/src/room_lighting.c)
/// Barycentric blend of the triangle's three corner colours (words 5-7) at
/// `position` into the three bytes of `color`; 0 when the point is outside.
pub fn roomLightTriangle(position: c_uint, color: [*]u8, triangle: [*]const c_uint) callconv(.c) c_int {
    const total = lightArea(triangle[2], triangle[4], triangle[3]);
    if (total <= 0) return 0;
    const a = lightArea(position, triangle[4], triangle[3]);
    if (a < 0) return 0;
    const c_area = lightArea(position, triangle[3], triangle[2]);
    if (c_area < 0) return 0;
    const unsigned_total: c_uint = @bitCast(total);
    const ua: c_uint = @bitCast(a);
    const uc: c_uint = @bitCast(c_area);
    const ub: c_uint = unsigned_total -% ua -% uc;
    if (@as(c_int, @bitCast(ub)) < 0) return 0;
    var channel: c_uint = 0;
    while (channel < 3) : (channel += 1) {
        const shift: u5 = @intCast(channel * 8); // channel < 3
        const sum = ((triangle[5] >> shift) & 255) *% ua +% ((triangle[7] >> shift) & 255) *% uc +% ((triangle[6] >> shift) & 255) *% ub;
        var value = sum / unsigned_total; // total > 0 here
        if (@as(c_int, @bitCast(value)) < 0) value = 0;
        if (value > 255) value = 255;
        color[channel] = @truncate(value);
    }
    return 1;
}
