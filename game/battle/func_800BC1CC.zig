//! The event camera's reset to a spherical position (src/BATTLE/BATTLE.PRG/4A0A8.c).
const std = @import("std");
const psx = @import("common").psx;
const v = @import("views_4A0A8");
const c = @import("c_4A0A8");

/// The scratchpad vector the C computes differences in (cameraScratch.v1 at 0x1F800098).
const v1: *v.VECTOR = @ptrFromInt(0x1F800098);

fn cv(p: *v.VECTOR) [*c]c.VECTOR {
    return psx.cptr(c.VECTOR, p);
}

fn vv(p: [*c]c.VECTOR) *v.VECTOR {
    return @ptrCast(@alignCast(p));
}

/// orig: _copyVector (src/BATTLE/BATTLE.PRG/4A0A8.c)
/// dst = src (vx, vy, vz).
fn copyVector(dst: *v.VECTOR, src: *v.VECTOR) *v.VECTOR {
    dst.vx = src.vx;
    dst.vy = src.vy;
    dst.vz = src.vz;
    return dst;
}

/// orig: _vectorMagnitude (src/BATTLE/BATTLE.PRG/4A0A8.c)
/// The length of a 20.12 vector, from its integer parts, back in 20.12.
fn vectorMagnitude(p: *v.VECTOR) c_int {
    var copy: v.VECTOR = undefined;
    _ = copyVector(&copy, p);
    const x = (copy.vx >> 12) *% (copy.vx >> 12);
    const y = (copy.vy >> 12) *% (copy.vy >> 12);
    const z = (copy.vz >> 12) *% (copy.vz >> 12);
    return c.vs_gte_rsqrt(x +% y +% z) *% 4096;
}

/// orig: func_800BC1CC (src/BATTLE/BATTLE.PRG/4A0A8.c)
/// Unless D_800F4BA0 asks to skip once (cleared), aims the eye and look-at at the spherical position
/// (arg0, arg1) and, when the move is long enough, starts both with D_800E9C1C's camera commands,
/// their duration the two distances' sum / 0x28000.
pub fn func_800BC1CC(arg0: c_short, arg1: c_int) callconv(.c) void {
    if (v.g.D_800F4BA0().* != 0) {
        v.g.D_800F4BA0().* = 0;
        return;
    }
    var eye = std.mem.zeroes(v.VECTOR); // the C's stack locals, written by the callee
    var lookAt = std.mem.zeroes(v.VECTOR);
    c.vs_battle_initialiseCameraFromSpherical(cv(&eye), cv(&lookAt), arg0, psx.conv(i16, arg1));
    const cam = v.g.D_800F4BA4().*.?;
    _ = copyVector(&cam.eye.position, &eye);
    _ = copyVector(&cam.lookAt.position, &lookAt);
    const eyeDistance = vectorMagnitude(vv(c._vectorSubtract(cv(v1), cv(&cam.eye.current), cv(&cam.eye.position))));
    const duration = psx.conv(i16, @divTrunc(eyeDistance +% vectorMagnitude(vv(c._vectorSubtract(cv(v1), cv(&cam.lookAt.current), cv(&cam.lookAt.position)))), 0x28000));
    if ((@as(c_int, duration) << 16) != 0) {
        const commands = v.g.D_800E9C1C();
        commands[6] = psx.conv(u8, duration);
        commands[2] = commands[6];
        _ = c.func_800BC2E8(commands, 0);
        _ = c.func_800BC2E8(commands + 3, 0);
    }
}
