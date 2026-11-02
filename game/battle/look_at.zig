//! A look-at rotation from one point to another (src/BATTLE/BATTLE.PRG/146C.c).
const psx = @import("common").psx;
const v = @import("views_146C");
const c = @import("c_146C");

/// orig: D_800689F8 (vs_battle_lookAt's `static const VECTOR`; GCC folded it, PORTING.md §6.3): up is -y.
const up = v.VECTOR{ .vx = 0, .vy = -4096, .vz = 0, .pad = 0 };

/// orig: vs_battle_lookAt (src/BATTLE/BATTLE.PRG/146C.c)
/// The rotation whose columns are the side (forward x up), the up (side x forward) and the forward direction
/// (to - from), each normalised with VectorNormal.
pub fn lookAt(from: ?*v.VECTOR, to: ?*v.VECTOR, mat: ?*v.MATRIX) callconv(.c) void {
    var direction: v.VECTOR = undefined;
    var side: v.VECTOR = undefined;
    var forward: v.VECTOR = undefined;
    var upward: v.VECTOR = up;
    direction.vx = to.?.vx -% from.?.vx;
    direction.vy = to.?.vy -% from.?.vy;
    direction.vz = to.?.vz -% from.?.vz;
    _ = c.VectorNormal(@ptrCast(&direction), @ptrCast(&forward));
    c.OuterProduct0(@ptrCast(&forward), @ptrCast(&upward), @ptrCast(&side));
    _ = c.VectorNormal(@ptrCast(&side), @ptrCast(&side));
    c.OuterProduct0(@ptrCast(&side), @ptrCast(&forward), @ptrCast(&upward));
    _ = c.VectorNormal(@ptrCast(&upward), @ptrCast(&upward));
    const m = &mat.?.m;
    m[0][0] = psx.conv(i16, side.vx);
    m[1][0] = psx.conv(i16, side.vy);
    m[2][0] = psx.conv(i16, side.vz);
    m[0][1] = psx.conv(i16, upward.vx);
    m[1][1] = psx.conv(i16, upward.vy);
    m[2][1] = psx.conv(i16, upward.vz);
    m[0][2] = psx.conv(i16, forward.vx);
    m[1][2] = psx.conv(i16, forward.vy);
    m[2][2] = psx.conv(i16, forward.vz);
}
