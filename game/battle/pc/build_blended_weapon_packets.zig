//! Weapon and shield model draw with blending modes (D2: 4B16C.s; pc/src/weapon_packets.c).
const common = @import("common");
const psx = common.psx;

const NativeTake = *const fn (c_int, ?*const anyopaque, ?*const anyopaque, ?*const anyopaque, ?*const anyopaque) callconv(.c) c_int;

/// orig: func_800B516C (pc/src/weapon_packets.c)
/// The blended variant of vs_render_drawModelFlatPolygons (material groups, the alternate CLUT for flagged groups, the blend mode of object byte 9): drawn through the native list, no packets are built.
pub fn buildBlendedWeaponPackets(projected: [*]u8, object: [*]u8, color: [*]u8, model: [*]u8) callconv(.c) void {
    if (psx.weak(NativeTake, "pc_object_native_take")) |take| _ = take(2, projected, object, model, color);
}
