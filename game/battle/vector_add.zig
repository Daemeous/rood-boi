//! Vector addition: out = a + b, componentwise (src/BATTLE/BATTLE.PRG/4A0A8.c).
const v = @import("views_4A0A8");

/// orig: _vectorAdd (src/BATTLE/BATTLE.PRG/4A0A8.c)
/// Stores a + b componentwise into out and returns out.
pub fn vectorAdd(out: ?*v.VECTOR, a: ?*v.VECTOR, b: ?*v.VECTOR) callconv(.c) ?*v.VECTOR {
    const o = out.?;
    const x = a.?;
    const y = b.?;
    o.vx = x.vx +% y.vx;
    o.vy = x.vy +% y.vy;
    o.vz = x.vz +% y.vz;
    return out;
}
