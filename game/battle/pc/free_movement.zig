//! Free movement toward a heading (D2: 7BDD4.s; oracle pc/src/battle_movement.c).
const c = @import("c_battle_movement");

/// orig: func_800E4B70 (pc/src/battle_movement.c)
/// Held actors (0x1A) go to func_800E1908; with a gate held (0x19B) the route
/// continues through it; otherwise sets byte 2, clears scratchpad byte 0x3D0
/// and starts free movement (func_800E3600 mode 1) at `angle & 0xFFF`.
pub fn freeMovement(state: [*]u8, angle: c_int) callconv(.c) void {
    if (state[0x1a] != 0) {
        c.func_800E1908(state, angle);
        return;
    }
    if (state[0x19b] != 0) {
        c.pc_route_continuation(state, 0);
        return;
    }
    state[2] = 1;
    @as(*u8, @ptrFromInt(0x1f800000 + 0x3d0)).* = 0;
    c.func_800E3600(state, angle & 0xfff, 1);
}
