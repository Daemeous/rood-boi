//! Cosine between an actor's facing and the direction to a point (D2: 7B5DC.s; oracle pc/src/battle_gte.c).
const c = @import("c_battle_gte");
const v = @import("views_battle_gte");

/// orig: facing_trig (pc/src/battle_gte.c)
/// The table's sine/cosine of a 12-bit angle (Q12): interleaved 4-byte entries,
/// folded by quadrant. Bits above 0xFFF are ignored; the angle is reflected
/// before the final sign test, which reads the reflected value.
fn facingTrig(angle_in: c_uint) c_int {
    var angle = angle_in;
    var value: c_int = undefined;
    if (angle & 0x400 != 0 and angle & 0x3ff == 0) {
        value = 0x1000;
    } else {
        var offset: usize = 0;
        if (angle & 0x400 != 0) angle = 0x800 -% angle;
        var index: usize = angle & 0x1ff;
        if (angle & 0x200 != 0) {
            index = 0x200 - index;
            offset = 2;
        }
        value = @as(*align(1) const u16, @ptrCast(v.g._trig_table() + offset + index * 4)).*;
    }
    return if (angle & 0x800 != 0) -%value else value;
}

/// orig: func_800E437C (pc/src/battle_gte.c)
/// The cosine (Q12, clamped to +-0x1000) between the facing `angle` and the XZ
/// direction from `from` to `to` (shorts X, Y, Z); 0x1000 when `distance` is
/// zero. The direction is normalised by VectorNormal.
pub fn facingCosine(from: [*]const i16, to: [*]const i16, angle: c_int, distance: c_uint) callconv(.c) c_int {
    if (distance == 0) return 0x1000;
    const base: c_uint = @bitCast((0xc00 -% angle) & 0xfff);
    const z_weight = facingTrig(base);
    const x_weight = facingTrig(base +% 0x400);
    var direction = c.VECTOR{ .vx = @as(c_int, to[0]) - @as(c_int, from[0]), .vy = 0, .vz = @as(c_int, to[2]) - @as(c_int, from[2]), .pad = 0 };
    var unit: c.VECTOR = undefined;
    _ = c.VectorNormal(&direction, &unit);
    const sum: c_uint = @as(c_uint, @bitCast(unit.vx)) *% @as(c_uint, @bitCast(x_weight)) +%
        @as(c_uint, @bitCast(unit.vz)) *% @as(c_uint, @bitCast(z_weight));
    const result = @as(c_int, @bitCast(sum)) >> 12;
    if (result < -0x1000) return -0x1000;
    if (result >= 0x1000) return 0x1000;
    return result;
}
