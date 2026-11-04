//! Steering to the packed target tile (D2: 7BDD4.s; oracle pc/src/battle_movement.c).
const c = @import("c_battle_movement");
const psx = @import("common").psx;

/// orig: func_800E4B2C (pc/src/battle_movement.c)
/// With a gate held (0x19B) continues the route through it; on the packed
/// target tile (0x168 against 0x34, bits 8-15 ignored) calls func_800E2CCC;
/// otherwise stores the target's centre at 0x17C/0x180 and calls func_800E19FC.
pub fn steerToTile(state: [*]u8, angle: c_int) callconv(.c) void {
    if (state[0x19b] != 0) {
        c.pc_route_continuation(state, 0);
        return;
    }
    const target: c_uint = psx.byteAt(c_uint, state, 0x168).* & 0xff00ff;
    if (target == (psx.byteAt(c_uint, state, 0x34).* & 0xff00ff)) {
        c.func_800E2CCC(state, angle);
        return;
    }
    psx.byteAt(c_uint, state, 0x17c).* = (target & 0xff) *% 128 +% 64;
    psx.byteAt(c_uint, state, 0x180).* = (target >> 9) +% 64;
    c.func_800E19FC(state, angle);
}
