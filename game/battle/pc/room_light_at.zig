//! Room lighting lookup at an actor's position (D2: 2EA3C.s; oracle pc/src/room_lighting.c).
const c = @import("c_room_lighting");

/// orig: vs_render_sampleRoomLighting (pc/src/room_lighting.c)
/// Looks the light colour of `position` up in the room's triangle table (the room's
/// lightingSection, enabled by the signed byte at +0).
pub fn roomLightAt(position: c_uint, layer: c_int, color: [*]c_uint) callconv(.c) c_int {
    var enabled: c_int = undefined;
    const table = c.pc_room_light_table(&enabled);
    return c.pc_room_light(position, layer, color, table, enabled);
}
