//! The view matrix from the camera (pc/src/game/BATTLE/BATTLE.PRG/146C.c).
const psx = @import("common").psx;
const gte = @import("common").gte;
const v = @import("views_146C");
const c = @import("c_146C");

const LiveCamera = *const fn ([*c]c.VECTOR, [*c]c.VECTOR) callconv(.c) c_int;

/// orig: vs_render_worldUp (vs_render_buildCameraViewMatrix's `static const VECTOR`; GCC folded it, PORTING.md §6.3): up is +y.
const up_axis = v.VECTOR{ .vx = 0, .vy = 4096, .vz = 0, .pad = 0 };

/// The low 16 bits of a component, as the C's `*(u_short*)&v.vx` reads them.
fn low(x: i32) c_int {
    return @as(u16, @truncate(@as(u32, @bitCast(x))));
}

/// orig: vs_render_buildCameraViewMatrix (pc/src/game/BATTLE/BATTLE.PRG/146C.c)
/// Builds the view matrix (and its copy vs_render_cameraBasisMatrix) from the camera's position and look-at (the editor's live
/// camera, pc_live_camera, may replace both): the side, up and forward axes (x negated as the C stores them), rolled by angles.vz and stretched 4608/4096 across, translated to the camera, all scaled by the far clip.
pub fn vs_render_buildCameraViewMatrix() callconv(.c) void {
    var delta: v.VECTOR = undefined;
    var right: v.VECTOR = undefined;
    var forward: v.VECTOR = undefined;
    var up = up_axis;
    var negative: v.SVECTOR = undefined;
    var roll: v.MATRIX = undefined;
    const view = v.g.vs_view();
    // ROOD_PC: the editor's live camera (pc_live_camera) builds the view instead; the game's camera is kept.
    var position = view.camera.position;
    var lookAt = view.camera.lookAt;
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
    const copy = psx.Global(v.MATRIX, "vs_render_cameraBasisMatrix").ptr();
    const viewMatrix = &view.viewMatrix;
    const axes = [3]*v.VECTOR{ &right, &up, &forward };
    for (axes, 0..) |axis, row| {
        viewMatrix.m[row][0] = psx.conv(i16, -low(axis.vx));
        copy.m[row][0] = viewMatrix.m[row][0];
        viewMatrix.m[row][1] = psx.conv(i16, low(axis.vy));
        copy.m[row][1] = viewMatrix.m[row][1];
        viewMatrix.m[row][2] = psx.conv(i16, low(axis.vz));
        copy.m[row][2] = viewMatrix.m[row][2];
    }
    roll.m[0][0] = psx.conv(i16, c.rcos(view.camera.angles.vz));
    roll.m[1][1] = roll.m[0][0];
    roll.m[1][0] = psx.conv(i16, c.rsin(view.camera.angles.vz));
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
    c.vs_core_mathMultiplyMatrixIntoRight(@ptrCast(&roll), @ptrCast(viewMatrix));
    negative.vx = psx.conv(i16, @divTrunc(-%position.vx, 4096));
    negative.vy = psx.conv(i16, @divTrunc(-%position.vy, 4096));
    negative.vz = psx.conv(i16, @divTrunc(-%position.vz, 4096));
    gte.applyMatrix(&viewMatrix.t, viewMatrix, &negative);
    delta.vx = view.camera.farClip;
    delta.vy = delta.vx;
    delta.vz = delta.vy;
    _ = c.ScaleMatrix(@ptrCast(viewMatrix), @ptrCast(&delta));
    viewMatrix.t[0] = @divTrunc(viewMatrix.t[0] *% view.camera.farClip, 4096);
    viewMatrix.t[1] = @divTrunc(viewMatrix.t[1] *% view.camera.farClip, 4096);
    viewMatrix.t[2] = @divTrunc(viewMatrix.t[2] *% view.camera.farClip, 4096);
}
