//! Aiming at the movement point (D2: 7C7E0.s; oracle pc/src/battle_movement.c).
const ai = @import("views_71618");
const c = @import("c_battle_movement");
const psx = @import("common").psx;

/// orig: func_800E4FE0 (pc/src/battle_movement.c)
/// Aims at the point (0x17C, 0x180), records it packed at 0x158, marks
/// movement (byte 1 = kind, byte 9 = 1) and dispatches with the heading
/// toward it.
pub fn aimAtPoint(state: *ai.vs_battle_aiState, kind: c_int) callconv(.c) void {
    const x: c_int = @bitCast(state.moveX);
    const z: c_int = @bitCast(state.moveZ);
    state.flags0.b[1] = psx.conv(u8, kind);
    state.flags8.b[1] = 1;
    @as(*align(1) c_uint, @ptrCast(&state.targetX)).* = @as(c_uint, @bitCast(x)) | (@as(c_uint, @bitCast(z)) << 16);
    const heading = c.ratan2(
        x -% @as(c_int, state.position.xyz[0]),
        z -% @as(c_int, state.position.xyz[2]),
    );
    c.vs_battle_aiDispatchAiLocomotionMode(state, 1, heading);
}
