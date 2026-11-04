//! Aiming at the movement point (D2: 7C7E0.s; oracle pc/src/battle_movement.c).
const c = @import("c_battle_movement");
const psx = @import("common").psx;

/// orig: func_800E4FE0 (pc/src/battle_movement.c)
/// Aims at the point (0x17C, 0x180), records it packed at 0x158, marks
/// movement (byte 1 = kind, byte 9 = 1) and dispatches with the heading
/// toward it.
pub fn aimAtPoint(state: [*]u8, kind: c_int) callconv(.c) void {
    const x: c_int = @bitCast(psx.byteAt(c_uint, state, 0x17c).*);
    const z: c_int = @bitCast(psx.byteAt(c_uint, state, 0x180).*);
    state[1] = psx.conv(u8, kind);
    state[9] = 1;
    psx.byteAt(c_uint, state, 0x158).* = @as(c_uint, @bitCast(x)) | (@as(c_uint, @bitCast(z)) << 16);
    const heading = c.ratan2(
        x -% @as(c_int, psx.byteAt(i16, state, 0x4c).*),
        z -% @as(c_int, psx.byteAt(i16, state, 0x50).*),
    );
    c.func_800E50A0(state, 1, heading);
}
