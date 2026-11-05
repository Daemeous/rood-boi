//! Room render rules: the near depth bucket (D2: 2EA3C.s; oracle pc/src/room_render.c).
const v = @import("views_room_render");
const psx = @import("common").psx;

/// orig: func_80098160 (pc/src/room_render.c)
/// Sets the room renderer's nearest depth bucket (a short).
pub fn setRoomNearDepth(value: c_int) callconv(.c) void {
    v.g.near_depth().* = psx.conv(i16, value);
}
