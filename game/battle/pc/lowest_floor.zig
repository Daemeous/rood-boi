//! Lowest floor height along a line (D2: 7E05C.s; oracle pc/src/battle_movement.c).
const c = @import("c_battle_movement");

/// orig: func_800E6A6C (pc/src/battle_movement.c)
/// Lowest of 16 floor heights sampled from (x0, z0) toward (x1, z1) in steps
/// of a sixteenth, starting at 0 (heights are 15-bit signed).
pub fn lowestFloorAlong(x0: c_int, z0: c_int, x1: c_int, z1: c_int) callconv(.c) c_int {
    const dx = (x1 -% x0) >> 4;
    const dz = (z1 -% z0) >> 4;
    var x = x0;
    var z = z0;
    var lowest: c_int = 0;
    var i: c_int = 0;
    while (i < 16) : (i += 1) {
        x +%= dx;
        z +%= dz;
        const raw: c_uint = @bitCast(c.func_800E4318(x, z));
        const height = @as(c_int, @bitCast(raw << 17)) >> 17;
        if (height <= lowest) lowest = height;
    }
    return lowest;
}
