//! Component-wise sum of a VECTOR and an SVECTOR into a VECTOR (src/BATTLE/BATTLE.PRG/5BF94.c).
const v = @import("views_5BF94");

/// orig: vs_battle_addVecToSvec (src/BATTLE/BATTLE.PRG/5BF94.c)
/// Writes `a + b` component-wise into `out`, each sum wrapping at 32 bits.
pub fn addVecToSvec(arg0: ?*v.VECTOR, arg1: ?*v.SVECTOR, arg2: ?*v.VECTOR) callconv(.c) void {
    arg2.?.vx = arg0.?.vx +% @as(i32, arg1.?.vx);
    arg2.?.vy = arg0.?.vy +% @as(i32, arg1.?.vy);
    arg2.?.vz = arg0.?.vz +% @as(i32, arg1.?.vz);
}
