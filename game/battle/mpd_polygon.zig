//! A textured polygon record of the room geometry (MPD section 1), as the animated and scrolling textures write it. The record
//! is read by the renderer from the room's raw geometry section, so it is a view over the file's bytes, not a copy; this file
//! is the one place that names its fields (any byte not named here is not touched by the port's own code).
//!
//!   +0x0F  code byte: (code & 0x3C) == 0x34 is a triangle (0x20 bytes per record), else a quad (0x28 bytes)
//!   +0x18, +0x1C, +0x13  u of the first, second and third vertex;  +0x19, +0x1D, +0x17  their v
//!   +0x23  u of the fourth vertex of a quad;  +0x27  its v
//!   +0x1E  i16 texture id (offsetRoomTextureIds adds a room's base to it)
pub const Polygon = [*]align(1) u8;

pub inline fn at(base: usize, offset: isize) Polygon {
    return @ptrFromInt(base +% @as(usize, @bitCast(offset)));
}

pub inline fn isTriangle(p: Polygon) bool {
    return (p[0xF] & 0x3C) == 0x34;
}

/// Writes the vertices' u and v (the fourth only for a quad). `u` and `v` have the animation's per-vertex bases; `du` and `dv` the window's position.
pub fn setUv(p: Polygon, u: [4]u8, v: [4]u8, du: c_int, dv: c_int) void {
    p[0x18] = trunc(@as(c_int, u[0]) +% du);
    p[0x1C] = trunc(@as(c_int, u[1]) +% du);
    p[0x13] = trunc(@as(c_int, u[2]) +% du);
    p[0x19] = trunc(@as(c_int, v[0]) +% dv);
    p[0x1D] = trunc(@as(c_int, v[1]) +% dv);
    p[0x17] = trunc(@as(c_int, v[2]) +% dv);
    if (!isTriangle(p)) {
        p[0x23] = trunc(@as(c_int, u[3]) +% du);
        p[0x27] = trunc(@as(c_int, v[3]) +% dv);
    }
}

inline fn trunc(x: c_int) u8 {
    return @truncate(@as(c_uint, @bitCast(x)));
}
