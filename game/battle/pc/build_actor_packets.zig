//! Actor model draw (the per-vertex-colour model) (D2: 4B16C.s; pc/src/actor_packets.c).
const common = @import("common");
const psx = common.psx;

const NativeTake = *const fn (c_int, ?*const anyopaque, ?*const anyopaque, ?*const anyopaque, ?*const anyopaque) callconv(.c) c_int;

/// orig: vs_render_drawModelGouraudPolygons (pc/src/actor_packets.c)
/// Draws the actor's model from the projected vertices (screen words, then depths 0xC00 bytes on) through the native list: no packets are built (actor_native.c pc_object_native_take). `unused` is not read.
pub fn buildActorPackets(projected: [*]c_uint, object: [*]u8, unused: ?*anyopaque, model: [*]u8) callconv(.c) void {
    _ = unused;
    if (psx.weak(NativeTake, "pc_object_native_take")) |take| _ = take(1, projected, object, model, null);
}
