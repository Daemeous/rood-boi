//! Room view transforms with the scratchpad camera (D2: 2EA3C.s; oracle pc/src/boot_sdk.c).
const common = @import("common");
const gte = common.gte;
const c = @import("c_boot_sdk");

/// orig: vs_render_buildRoomGeometryViewMatrix (pc/src/boot_sdk.c)
/// The room transform of a position and YXZ angles (VECTORs) composed with the
/// camera rotation at scratchpad 0x1F800014, into `result`.
pub fn roomTransform(position: [*c]c.VECTOR, angles: [*c]c.VECTOR, result: [*c]c.MATRIX) callconv(.c) void {
    const frame = gte.frame();
    c.pc_room_transform(position, angles, common.view.at(c.MATRIX, common.view.matrix_offset), result, &frame.t);
}

/// orig: vs_render_buildScaleCompensatedRoomView (pc/src/boot_sdk.c)
/// The same with the far-clip scale of scratchpad word 0x1F800064 applied to the camera first.
pub fn scaledRoomTransform(position: [*c]c.VECTOR, angles: [*c]c.VECTOR, result: [*c]c.MATRIX) callconv(.c) void {
    const far_clip = common.view.at(c_int, common.view.far_clip_offset).*;
    const frame = gte.frame();
    c.pc_scaled_room_transform(position, angles, common.view.at(c.MATRIX, common.view.matrix_offset), far_clip, result, &frame.t);
}
