//! Room render rules: the far depth bucket (D2: 2EA3C.s; oracle pc/src/room_render.c).
const v = @import("views_room_render");
const psx = @import("common").psx;

/// orig: func_8009817C (pc/src/room_render.c)
/// Sets the room renderer's farthest depth bucket (a short).
pub fn setRoomFarDepth(value: c_int) callconv(.c) void {
    v.g.far_depth().* = psx.conv(i16, value);
}
