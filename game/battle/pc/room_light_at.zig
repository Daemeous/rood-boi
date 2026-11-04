//! Room lighting lookup at an actor's position (D2: 2EA3C.s; oracle pc/src/room_lighting.c).
const c = @import("c_room_lighting");
const v = @import("views_room_lighting");
const psx = @import("common").psx;

/// orig: func_80098B38 (pc/src/room_lighting.c)
/// Looks the light colour of `position` up in the room's triangle table (the
/// guest pointer at room data +0x74, enabled by the signed byte at +0).
pub fn roomLightAt(position: c_uint, layer: c_int, color: [*]c_uint) callconv(.c) c_int {
    const room = v.g.vs_battle_roomData();
    const table: ?[*]const c_uint = @ptrFromInt(@as(usize, psx.byteAt(u32, room, 0x74).*));
    const enabled: c_int = @as(i8, @bitCast(room[0]));
    return c.pc_room_light(position, layer, color, table, enabled);
}
