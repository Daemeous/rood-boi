//! Projecting an object's bone vertices (D2: 4B16C.s; oracle pc/src/actor_vertices.c).
const c = @import("c_actor_vertices");
const v = @import("views_actor_vertices");
const psx = @import("common").psx;

const NativeMatrices = *const fn (?*const anyopaque, ?*const anyopaque, ?*const anyopaque) callconv(.c) void;

/// orig: func_800B3A68 (pc/src/actor_vertices.c)
/// Gives the native renderer the object's matrices, then projects the model's
/// vertex groups (guest pointers at model +0x34 and +0x38, one group count in
/// object byte 1) into the screen and depth arrays at D_800F2458 and +768
/// words; returns the visibility word. `near_clip` is unused, as in the
/// original.
pub fn projectObjectVertices(object: [*]u8, matrices: [*c]c.MATRIX, near_clip: c_int, model: [*]u8) callconv(.c) c_int {
    _ = near_clip;
    if (psx.weak(NativeMatrices, "pc_object_native_matrices")) |hook| hook(object, matrices, model);
    const groups: ?[*]const c_uint = @ptrFromInt(@as(usize, psx.byteAt(u32, model, 0x34).*));
    const vertices: ?[*]const c.SVECTOR = @ptrFromInt(@as(usize, psx.byteAt(u32, model, 0x38).*));
    const screen = v.g.D_800F2458();
    return @bitCast(c.pc_project_bone_vertices(object[1], groups, vertices, matrices, screen, screen + 768));
}
