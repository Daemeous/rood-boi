//! Room render rules: the level-of-detail distance (D2: 2EA3C.s; oracle pc/src/room_render.c).
const v = @import("views_room_render");
const psx = @import("common").psx;

/// orig: func_80098194 (pc/src/room_render.c)
/// Sets the room renderer's distance threshold (a short).
pub fn setRoomDistanceThreshold(value: c_int) callconv(.c) void {
    v.g.distance_threshold().* = psx.conv(i16, value);
}
