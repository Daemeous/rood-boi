//! Room view transforms with the scratchpad camera (D2: 2EA3C.s; oracle pc/src/boot_sdk.c).
const c = @import("c_boot_sdk");

/// orig: func_800985AC (pc/src/boot_sdk.c)
/// The room transform of a position and YXZ angles (VECTORs) composed with the
/// camera rotation at scratchpad 0x1F800014, into `result`.
pub fn roomTransform(position: [*c]c.VECTOR, angles: [*c]c.VECTOR, result: [*c]c.MATRIX) callconv(.c) void {
    c.pc_room_transform(position, angles, @ptrFromInt(0x1f800014), result);
}

/// orig: func_80098648 (pc/src/boot_sdk.c)
/// The same with the far-clip scale of scratchpad word 0x1F800064 applied to the camera first.
pub fn scaledRoomTransform(position: [*c]c.VECTOR, angles: [*c]c.VECTOR, result: [*c]c.MATRIX) callconv(.c) void {
    const far_clip = @as(*c_int, @ptrFromInt(0x1f800064)).*;
    c.pc_scaled_room_transform(position, angles, @ptrFromInt(0x1f800014), far_clip, result);
}
