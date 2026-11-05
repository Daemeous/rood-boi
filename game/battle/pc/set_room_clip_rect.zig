//! Room render rules: the clip rectangle (D2: 2EA3C.s; oracle pc/src/room_render.c).
const v = @import("views_room_render");
const psx = @import("common").psx;

/// orig: func_800981B8 (pc/src/room_render.c)
/// Sets the room renderer's clip rectangle (four shorts), left, top, right, bottom in that order.
pub fn setRoomClipRect(left: c_int, top: c_int, right: c_int, bottom: c_int) callconv(.c) void {
    v.g.clip_left().* = psx.conv(i16, left);
    v.g.clip_top().* = psx.conv(i16, top);
    v.g.clip_right().* = psx.conv(i16, right);
    v.g.clip_bottom().* = psx.conv(i16, bottom);
}
