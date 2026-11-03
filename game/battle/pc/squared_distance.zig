//! Distance between two actor positions (D2: 7BDD4.s; oracle pc/src/battle_helpers.c).
const c = @import("c_battle_helpers");

/// orig: func_800E4624 (pc/src/battle_helpers.c)
/// Squared distance between two short XYZ positions with a height offset
/// added to each Y: the component differences go to func_800E4660 (the GTE
/// squares them), whose result is returned.
pub fn squaredDistance(a: [*]const i16, b: [*]const i16, a_height: c_int, b_height: c_int) callconv(.c) c_int {
    const dx: c_int = @as(c_int, a[0]) - b[0];
    const dy: c_int = (@as(c_int, a[1]) +% a_height) -% (@as(c_int, b[1]) +% b_height);
    const dz: c_int = @as(c_int, a[2]) - b[2];
    return c.func_800E4660(dx, dy, dz);
}
