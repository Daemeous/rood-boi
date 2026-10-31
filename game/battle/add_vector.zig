//! Component-wise sum of two vectors into a third (src/BATTLE/BATTLE.PRG/5BF94.c).
const v = @import("views_5BF94");

/// orig: vs_battle_addVector (src/BATTLE/BATTLE.PRG/5BF94.c)
/// Stores the component-wise sum of `a` and `b` in `out`.
pub fn addVector(a: ?*v.VECTOR, b: ?*v.VECTOR, out: ?*v.VECTOR) callconv(.c) void {
    out.?.vx = a.?.vx +% b.?.vx;
    out.?.vy = a.?.vy +% b.?.vy;
    out.?.vz = a.?.vz +% b.?.vz;
}
