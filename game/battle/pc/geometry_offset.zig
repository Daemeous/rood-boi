//! GTE screen offset access (D2: 2EA3C.s; oracle pc/src/room_geometry.c).
const c = @import("c_room_geometry");

/// orig: vs_battle_getGeomOffset (pc/src/room_geometry.c)
/// Reads the GTE screen offset (OFX/OFY, 16.16) as two integers.
pub fn getGeomOffset(offset: [*]c_uint) callconv(.c) void {
    offset[0] = c.CFC2(24) >> 16;
    offset[1] = c.CFC2(25) >> 16;
}

/// orig: vs_battle_setGeomOffset (pc/src/room_geometry.c)
/// Writes the GTE screen offset (OFX/OFY) from two integers.
pub fn setGeomOffset(offset: [*]const c_uint) callconv(.c) void {
    c.CTC2(offset[0] << 16, 24);
    c.CTC2(offset[1] << 16, 25);
}
