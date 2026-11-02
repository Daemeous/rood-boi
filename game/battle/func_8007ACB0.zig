//! The view matrix from the camera (src/BATTLE/BATTLE.PRG/146C.c).
const psx = @import("common").psx;
const v = @import("views_146C");
const c = @import("c_146C");

const LiveCamera = *const fn ([*c]c.VECTOR, [*c]c.VECTOR) callconv(.c) c_int;

/// orig: D_800689E8 (func_8007ACB0's `static const VECTOR`; GCC folded it, PORTING.md §6.3): up is +y.
const up_axis = v.VECTOR{ .vx = 0, .vy = 4096, .vz = 0, .pad = 0 };

/// The low 16 bits of a component, as the C's `*(u_short*)&v.vx` reads them.
fn low(x: i32) c_int {
    return @as(u16, @truncate(@as(u32, @bitCast(x))));
}

/// orig: func_8007ACB0 (src/BATTLE/BATTLE.PRG/146C.c)
/// Builds the view matrix (and its copy D_8005E218) from the camera's position and look-at (the editor's live
/// camera, pc_live_camera, may replace both): the side, up and forward axes (x negated as the C stores them), rolled by angles.vz and stretched 4608/4096 across, translated to the camera, all scaled by the far clip.
pub fn func_8007ACB0() callconv(.c) void {
    var delta: v.VECTOR = undefined;
    var right: v.VECTOR = undefined;
    var forward: v.VECTOR = undefined;
    var up = up_axis;
    var negative: v.SVECTOR = undefined;
    var roll: v.MATRIX = undefined;
    const scratch = v.g.vs_scratch();
    // ROOD_PC: the editor's live camera (pc_live_camera) builds the view instead; the game's camera is kept.
    var position = scratch.camera.position;
    var lookAt = scratch.camera.lookAt;
    if (psx.weak(LiveCamera, "pc_live_camera")) |live| {
        _ = live(@ptrCast(&position), @ptrCast(&lookAt));
    }
    delta.vx = @divTrunc(position.vx -% lookAt.vx, 4096);
    delta.vy = @divTrunc(lookAt.vy -% position.vy, 4096);
    delta.vz = @divTrunc(lookAt.vz -% position.vz, 4096);
    if ((delta.vx | delta.vz) == 0) {
        delta.vz = 1;
    }
    _ = c.VectorNormal(@ptrCast(&delta), @ptrCast(&forward));
    c.OuterProduct0(@ptrCast(&forward), @ptrCast(&up), @ptrCast(&right));
    _ = c.VectorNormal(@ptrCast(&right), @ptrCast(&right));
    c.OuterProduct0(@ptrCast(&right), @ptrCast(&forward), @ptrCast(&up));
    _ = c.VectorNormal(@ptrCast(&up), @ptrCast(&up));
    const copy = psx.global(v.MATRIX, "D_8005E218");
    const view = &scratch.viewMatrix;
    const axes = [3]*v.VECTOR{ &right, &up, &forward };
    for (axes, 0..) |axis, row| {
        view.m[row][0] = psx.conv(i16, -low(axis.vx));
        copy.m[row][0] = view.m[row][0];
        view.m[row][1] = psx.conv(i16, low(axis.vy));
        copy.m[row][1] = view.m[row][1];
        view.m[row][2] = psx.conv(i16, low(axis.vz));
        copy.m[row][2] = view.m[row][2];
    }
    roll.m[0][0] = psx.conv(i16, c.rcos(scratch.camera.angles.vz));
    roll.m[1][1] = roll.m[0][0];
    roll.m[1][0] = psx.conv(i16, c.rsin(scratch.camera.angles.vz));
    roll.m[0][1] = psx.conv(i16, -@as(c_int, roll.m[1][0]));
    roll.m[2][2] = 4096;
    roll.m[2][1] = 0;
    roll.m[2][0] = 0;
    roll.m[1][2] = 0;
    roll.m[0][2] = 0;
    delta.vx = 4608;
    delta.vy = 4096;
    delta.vz = 4096;
    _ = c.ScaleMatrix(@ptrCast(&roll), @ptrCast(&delta));
    c.func_80041C68(@ptrCast(&roll), @ptrCast(view));
    negative.vx = psx.conv(i16, @divTrunc(-%position.vx, 4096));
    negative.vy = psx.conv(i16, @divTrunc(-%position.vy, 4096));
    negative.vz = psx.conv(i16, @divTrunc(-%position.vz, 4096));
    _ = c.ApplyMatrix(@ptrCast(view), @ptrCast(&negative), @ptrCast(&view.t));
    delta.vx = scratch.camera.farClip;
    delta.vy = delta.vx;
    delta.vz = delta.vy;
    _ = c.ScaleMatrix(@ptrCast(view), @ptrCast(&delta));
    view.t[0] = @divTrunc(view.t[0] *% scratch.camera.farClip, 4096);
    view.t[1] = @divTrunc(view.t[1] *% scratch.camera.farClip, 4096);
    view.t[2] = @divTrunc(view.t[2] *% scratch.camera.farClip, 4096);
}
