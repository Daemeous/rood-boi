//! Builds the camera rotation and zoom matrix in the scratchpad and stores it
//! in D_800F49B8 with the source's translation (src/BATTLE/BATTLE.PRG/44F14.c).
const std = @import("std");
const common = @import("common");
const psx = common.psx;
const v = @import("views_44F14");
const c = @import("c_44F14");

/// orig: func_800AECA0 (src/BATTLE/BATTLE.PRG/44F14.c)
/// Builds the camera transform from the view matrix, rotation and zoom, and stores it in D_800F49B8 with the source's translation.
pub fn func_800AECA0(source: ?*v.MATRIX) callconv(.c) void {
    var rotation: v.MATRIX = std.mem.zeroes(v.MATRIX);
    const scratch: *v.cameraTransformScratch = @ptrFromInt(0x1F80035C);
    const camera_angle: *c_int = @ptrFromInt(0x1F80005C);
    const camera_zoom: *c_int = @ptrFromInt(0x1F800064);

    {
        var i: usize = 0;
        while (i < 3) : (i += 1) {
            scratch.matrix.m[0][i] = v.g.D_8005E218().m[i][0];
            scratch.matrix.m[1][i] = v.g.D_8005E218().m[i][1];
            scratch.matrix.m[2][i] = v.g.D_8005E218().m[i][2];
        }
    }

    const neg_angle: c_int = -%camera_angle.*;
    const cos_v: i16 = psx.conv(i16, c.rcos(neg_angle));
    rotation.m[1][1] = cos_v;
    rotation.m[0][0] = cos_v;
    rotation.m[1][0] = psx.conv(i16, c.rsin(neg_angle));
    rotation.m[0][1] = psx.conv(i16, -%@as(c_int, rotation.m[1][0]));
    rotation.m[2][2] = 4096;
    rotation.m[0][2] = 0;
    rotation.m[1][2] = 0;
    rotation.m[2][0] = 0;
    rotation.m[2][1] = 0;

    scratch.scale.vx = 3640;
    scratch.scale.vy = 4096;
    scratch.scale.vz = 4096;
    _ = c.ScaleMatrix(psx.cptr(c.MATRIX, &scratch.matrix), psx.cptr(c.VECTOR, &scratch.scale));
    c.func_80041C68(psx.cptr(c.MATRIX, &scratch.matrix), psx.cptr(c.MATRIX, &rotation));
    scratch.matrix = rotation;

    const i: c_int = @divTrunc(@as(c_int, 0x1000000), camera_zoom.*);
    scratch.scale.vx = i;
    scratch.scale.vy = i;
    scratch.scale.vz = i;
    _ = c.ScaleMatrix(psx.cptr(c.MATRIX, &scratch.matrix), psx.cptr(c.VECTOR, &scratch.scale));

    // ROOD_PC: the first word holds the source's guest address, the rest of D_800F4910 is zero.
    const src = source.?;
    v.g.D_800F4910().* = std.mem.zeroes(v.MATRIX);
    @as(*c_uint, @ptrCast(v.g.D_800F4910())).* = psx.addr(src);

    v.g.D_800F49B8().* = scratch.matrix;
    v.g.D_800F49B8().t[0] = src.t[0];
    v.g.D_800F49B8().t[1] = src.t[1];
    v.g.D_800F49B8().t[2] = src.t[2];
}
