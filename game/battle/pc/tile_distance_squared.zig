//! Squared tile distance (D2: 7BDD4.s; oracle pc/src/battle_movement.c).
const c = @import("c_battle_movement");

/// orig: vs_core_mathSquaredTileDistance (pc/src/battle_movement.c)
/// Squared tile distance between two packed tiles (x byte 0, z byte 2): the
/// differences go to vs_core_mathSquaredVectorLength, whose result is returned.
pub fn tileDistanceSquared(a: c_uint, b: c_uint) callconv(.c) c_int {
    // Each operand is a byte, so the differences cannot overflow.
    const dx: c_int = @as(c_int, @intCast(a & 0xff)) - @as(c_int, @intCast(b & 0xff));
    const dz: c_int = @as(c_int, @intCast((a >> 16) & 0xff)) - @as(c_int, @intCast((b >> 16) & 0xff));
    return c.vs_core_mathSquaredVectorLength(dx, 0, dz);
}
