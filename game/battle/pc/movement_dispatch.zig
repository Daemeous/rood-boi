//! Movement dispatch by priority state (D2: 7C7E0.s; oracle pc/src/battle_movement.c).
const c = @import("c_battle_movement");

/// orig: func_800E50A0 (pc/src/battle_movement.c)
/// Dispatches `angle` through the six movement functions (jtbl_800F1740) by
/// priority state (0x125) * 2 + `kind`. An index past the table is reported
/// as the original does; its call through the missing entry is not ported.
pub fn dispatchMovement(state: [*]u8, kind: c_int, angle: c_int) callconv(.c) void {
    const index: c_uint = @as(c_uint, state[0x125]) *% 2 +% @as(c_uint, @bitCast(kind));
    switch (index) {
        0 => c.func_800E4B2C(state, angle),
        1 => c.func_800E4B70(state, angle),
        2 => c.func_800E0A68(state, angle),
        3 => c.func_800E1238(state, angle),
        4 => c.func_800E153C(state, angle),
        5 => c.func_800E1764(state, angle),
        else => c.pc_unimplemented("actor movement dispatch index"),
    }
}
