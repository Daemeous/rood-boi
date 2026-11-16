//! Room tint (D2: 2EA3C.s vs_render_tintRoomGeometryPrimitives; the native list's tint is pc/src/room_native.c pc_room_native_tint).
const psx = @import("common").psx;

const NativeTint = *const fn ([*c]const c_uint, c_int, c_int, c_int, c_int) callconv(.c) void;

/// orig: vs_render_tintRoomGeometryPrimitives (pc/src/room_geometry.c)
/// Tints the room list built from `start`: offsets are signed bytes * 4 and the fade is a GTE DPCS toward the luminance. The room's
/// polygons are no longer packets (the opaque ones are in the native list, the translucent ones are markers), so there is no
/// packet colour to change.
pub fn tintRoomPackets(start: [*]c_uint, end: [*]c_uint, params: [*]const i8, flags: c_int) callconv(.c) void {
    _ = end;
    _ = flags;
    const dr: c_int = @as(c_int, params[0]) *% 4;
    const dg: c_int = @as(c_int, params[1]) *% 4;
    const db: c_int = @as(c_int, params[2]) *% 4;
    const fade: c_int = @as(*align(1) const i16, @ptrCast(params + 4)).*;
    if (psx.weak(NativeTint, "pc_room_native_tint")) |tint| tint(start, dr, dg, db, fade);
}
