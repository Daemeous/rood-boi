//! A look-at view matrix (src/BATTLE/BATTLE.PRG/5BF94.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_5BF94");
const c = @import("c_5BF94");

/// orig: func_800D197C (src/BATTLE/BATTLE.PRG/5BF94.c)
/// The view matrix looking from `position` at `target` (rows: -right, up, forward, normalised by libgte), its translation the rotated position negated.
pub fn func_800D197C(position: ?*v.VECTOR, target: ?*v.VECTOR, view_arg: ?*v.MATRIX) callconv(.c) void {
    const view = view_arg.?;
    var up = v.VECTOR{ .vx = 0, .vy = -4096, .vz = 0, .pad = 0 }; // initialUp (a folded static const)
    var right: v.VECTOR = undefined;
    var forward: v.VECTOR = undefined;
    var translation: v.VECTOR = undefined;
    forward.vx = target.?.vx -% position.?.vx;
    forward.vy = target.?.vy -% position.?.vy;
    forward.vz = target.?.vz -% position.?.vz;
    _ = c.VectorNormal(@ptrCast(&forward), @ptrCast(&forward));
    c.OuterProduct0(@ptrCast(&up), @ptrCast(&forward), @ptrCast(&right));
    _ = c.VectorNormal(@ptrCast(&right), @ptrCast(&right));
    c.OuterProduct0(@ptrCast(&right), @ptrCast(&forward), @ptrCast(&up));
    _ = c.VectorNormal(@ptrCast(&up), @ptrCast(&up));
    view.m[0][0] = psx.conv(i16, -%right.vx);
    view.m[0][1] = psx.conv(i16, -%right.vy);
    view.m[0][2] = psx.conv(i16, -%right.vz);
    view.m[1][0] = psx.conv(i16, up.vx);
    view.m[1][1] = psx.conv(i16, up.vy);
    view.m[1][2] = psx.conv(i16, up.vz);
    view.m[2][0] = psx.conv(i16, forward.vx);
    view.m[2][1] = psx.conv(i16, forward.vy);
    view.m[2][2] = psx.conv(i16, forward.vz);
    _ = c.ApplyMatrixLV(@ptrCast(view), @ptrCast(position), @ptrCast(&translation));
    view.t[0] = -%translation.vx;
    view.t[1] = -%translation.vy;
    view.t[2] = -%translation.vz;
}
