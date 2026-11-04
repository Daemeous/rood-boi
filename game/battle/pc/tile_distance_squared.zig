//! Squared tile distance (D2: 7BDD4.s; oracle pc/src/battle_movement.c).
const c = @import("c_battle_movement");

/// orig: func_800E45F4 (pc/src/battle_movement.c)
/// Squared tile distance between two packed tiles (x byte 0, z byte 2): the
/// differences go to func_800E4660, whose result is returned.
pub fn tileDistanceSquared(a: c_uint, b: c_uint) callconv(.c) c_int {
    // Each operand is a byte, so the differences cannot overflow.
    const dx: c_int = @as(c_int, @intCast(a & 0xff)) - @as(c_int, @intCast(b & 0xff));
    const dz: c_int = @as(c_int, @intCast((a >> 16) & 0xff)) - @as(c_int, @intCast((b >> 16) & 0xff));
    return c.func_800E4660(dx, 0, dz);
}
