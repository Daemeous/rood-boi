//! Semi-transparent actor model draw (D2: 4B16C.s; pc/src/actor_packets.c).
const common = @import("common");
const psx = common.psx;

const NativeTake = *const fn (c_int, ?*const anyopaque, ?*const anyopaque, ?*const anyopaque, ?*const anyopaque) callconv(.c) c_int;

/// orig: vs_render_drawModelGouraudPolygonsWithOverrides (pc/src/actor_packets.c)
/// The blended variant of vs_render_drawModelGouraudPolygons (object byte 9 selects the blend mode, every vertex takes the object colour): drawn through the native list, no packets are built.
pub fn buildBlendedActorPackets(projected: [*]c_uint, object: [*]u8, color: [*]const c_uint, model: [*]u8) callconv(.c) void {
    if (psx.weak(NativeTake, "pc_object_native_take")) |take| _ = take(3, projected, object, model, color);
}
