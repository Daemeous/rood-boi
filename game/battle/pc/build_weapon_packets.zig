//! Weapon model draw (D2: 4B16C.s; pc/src/weapon_packets.c).
const common = @import("common");
const psx = common.psx;

const NativeTake = *const fn (c_int, ?*const anyopaque, ?*const anyopaque, ?*const anyopaque, ?*const anyopaque) callconv(.c) c_int;

/// orig: vs_render_drawModelFlatPolygons (pc/src/weapon_packets.c)
/// Draws the weapon's model from the projected vertices (screen words, then depths 0xC00 bytes on), coloured by the word at `color`, through the native list: no packets are built (actor_native.c pc_object_native_take).
pub fn buildWeaponPackets(projected: [*]c_uint, object: [*]u8, color: [*]const c_uint, model: [*]u8) callconv(.c) void {
    if (psx.weak(NativeTake, "pc_object_native_take")) |take| _ = take(0, projected, object, model, color);
}
