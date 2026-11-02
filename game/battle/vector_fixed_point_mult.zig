//! Fixed-point scaling of a battle vector (src/BATTLE/BATTLE.PRG/4A0A8.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_4A0A8");

/// _fixedPointMult (PC branch): a * b >> 12 in 64 bits, truncated to int.
fn fixedPointMult(a: c_int, b: c_int) c_int {
    // Two i32 operands cannot overflow i64; *% keeps the wrap rule anyway.
    const wide = (@as(i64, a) *% @as(i64, b)) >> 12;
    return psx.conv(c_int, wide);
}

/// orig: _vectorFixedPointMult (src/BATTLE/BATTLE.PRG/4A0A8.c)
/// Writes each component of `src` scaled by `scale` (4.12 fixed point) into `dest`; returns `dest`.
pub fn vectorFixedPointMult(dest: ?*v.VECTOR, src: ?*v.VECTOR, scale: c_int) callconv(.c) ?*v.VECTOR {
    const out = dest.?;
    const in = src.?;
    out.vx = fixedPointMult(in.vx, scale);
    out.vy = fixedPointMult(in.vy, scale);
    out.vz = fixedPointMult(in.vz, scale);
    return dest;
}
