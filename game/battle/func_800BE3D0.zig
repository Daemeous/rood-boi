//! The event camera's placement from the room's camera state flags (src/BATTLE/BATTLE.PRG/4A0A8.c).
const std = @import("std");
const psx = @import("common").psx;
const v = @import("views_4A0A8");
const c = @import("c_4A0A8");

fn cv(p: *v.VECTOR) [*c]c.VECTOR {
    return psx.cptr(c.VECTOR, p);
}

/// orig: _copyVector (src/BATTLE/BATTLE.PRG/4A0A8.c)
/// dst = src (vx, vy, vz).
fn copyVector(dst: *v.VECTOR, src: *v.VECTOR) *v.VECTOR {
    dst.vx = src.vx;
    dst.vy = src.vy;
    dst.vz = src.vz;
    return dst;
}

/// orig: func_800BE3D0 (src/BATTLE/BATTLE.PRG/4A0A8.c)
/// Puts the eye and look-at, raised by D_800E9C24[flag 0xB0], at the spherical position of direction
/// (flag 0xAF - 1 + arg0 * 2) & 7 (each looking at the other, the look-at settled), aims them at
/// direction flag 0xAF, and starts the move with the commands D_800E9C28 and D_800E9C2C.
pub fn func_800BE3D0(arg0: u8) callconv(.c) void {
    var lookAt = std.mem.zeroes(v.VECTOR); // the C's stack locals, written by the callee
    var aimLookAt = std.mem.zeroes(v.VECTOR);
    var eye = std.mem.zeroes(v.VECTOR);
    var aimEye = std.mem.zeroes(v.VECTOR);
    const direction = c.vs_battle_getStateFlag(0xAF);
    const distance = c.vs_battle_getStateFlag(0xB0);
    const before: c_int = @as(c_int, direction) - 1; // a byte minus 1: no overflow
    c.vs_battle_initialiseCameraFromSpherical(cv(&eye), cv(&lookAt), (before +% @as(c_int, arg0) * 2) & 7, @as(c_int, distance) + 1);
    eye.vy +%= @as(c_int, v.g.D_800E9C24()[distance]) << 12;
    lookAt.vy +%= @as(c_int, v.g.D_800E9C24()[distance]) << 12;
    const cam = v.g.D_800F4BA4().*.?;
    _ = copyVector(&cam.lookAt.current, &lookAt);
    _ = copyVector(&cam.lookAt.target, &eye);
    _ = copyVector(&cam.eye.target, &lookAt);
    _ = copyVector(&cam.eye.current, &eye);
    cam.lookAt.mode = 0xF;
    c.vs_battle_initialiseCameraFromSpherical(cv(&aimEye), cv(&aimLookAt), direction, @as(c_int, distance) + 1);
    const cam2 = v.g.D_800F4BA4().*.?;
    _ = copyVector(&cam2.eye.position, &aimEye);
    _ = copyVector(&cam2.lookAt.position, &aimLookAt);
    _ = c.func_800BC2E8(v.g.D_800E9C28(), 0);
    _ = c.func_800BC2E8(v.g.D_800E9C2C(), 0);
}
