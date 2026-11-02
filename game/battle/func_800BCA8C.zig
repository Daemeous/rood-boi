//! The event camera's per-frame move of one point (src/BATTLE/BATTLE.PRG/4A0A8.c).
const psx = @import("common").psx;
const v = @import("views_4A0A8");
const c = @import("c_4A0A8");

/// The scratchpad block the moves work in (cameraScratch at 0x1F800088).
const scratch: *v.cameraScratch = @ptrFromInt(0x1F800088);
const v1: *v.VECTOR = @ptrFromInt(0x1F800098); // scratch.v1
const v2: *v.VECTOR = @ptrFromInt(0x1F8000A8); // scratch.v2
const rotation: *v.SVECTOR = @ptrFromInt(0x1F8000D8); // scratch.rotation
const matrix: *v.MATRIX = @ptrFromInt(0x1F8000F8); // scratch.matrix

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

/// orig: _sVectorToFixedPointVector (src/BATTLE/BATTLE.PRG/4A0A8.c)
/// dst = src * ONE.
fn sVectorToFixedPointVector(dst: *v.VECTOR, src: *v.SVECTOR) *v.VECTOR {
    dst.vx = @as(c_int, src.vx) * 4096; // a short times 4096: no overflow
    dst.vy = @as(c_int, src.vy) * 4096;
    dst.vz = @as(c_int, src.vz) * 4096;
    return dst;
}

/// orig: _fixedPointMult (src/BATTLE/BATTLE.PRG/4A0A8.c)
/// a * b in 20.12, through a 64-bit product (the PC branch; the PS1's mult/mflo/mfhi).
fn fixedPointMult(a: c_int, b: c_int) c_int {
    return @truncate((@as(i64, a) * b) >> 12); // 32 x 32 bits: no 64-bit overflow
}

/// orig: _toNormalIntegerVector (src/BATTLE/BATTLE.PRG/4A0A8.c)
/// dst = the unit vector (VectorNormal) of src's integer parts.
fn toNormalIntegerVector(src: *v.VECTOR, dst: *v.VECTOR) void {
    var whole: v.VECTOR = undefined; // pad never written, as the C's
    whole.vx = src.vx >> 12;
    whole.vy = src.vy >> 12;
    whole.vz = src.vz >> 12;
    _ = c.VectorNormal(cv(&whole), cv(dst));
}

/// The easing curve `ease` at the point's time (func_800BFEBC).
fn eased(cam: *v.D_800F4BA4_t2, ease: u8) c_int {
    return c.func_800BFEBC(ease, cam.time, cam.duration);
}

/// orig: func_800BCA8C (src/BATTLE/BATTLE.PRG/4A0A8.c)
/// Steps a camera point (look-at or eye) by its mode: a linear (2) or eased-distance (3) move, an orbit
/// (4: around start, 5: around position), an actor-relative point (8), an offset (9) or the dolly
/// (11); then the follow blend, and at the end of its time mode 15 and the follow's drift.
pub fn func_800BCA8C(arg: ?*v.D_800F4BA4_t2, _: ?*v.D_800F4BA4_t2) callconv(.c) void {
    const cam = arg.?;
    if (cam.mode == 15) return;
    if (cam.mode != 11) {
        cam.time = psx.conv(u8, @as(c_int, cam.time) +% (v.g.vs_gametime_tickspeed().* >> 1));
        if (cam.time > cam.duration) cam.time = cam.duration;
    }
    const mode = cam.mode;
    switch (mode) {
        2 => {
            scratch.value = eased(cam, cam.ease1);
            _ = c._vectorFixedPointMult(cv(v1), cv(&cam.velocity), scratch.value);
            _ = c._vectorAdd(cv(&cam.computed), cv(&cam.start), cv(v1));
        },
        3 => {
            scratch.value = eased(cam, cam.ease1);
            _ = c._vectorFixedPointMult(cv(v1), cv(&cam.velocity), scratch.value);
            _ = c._vectorAdd(cv(v2), cv(&cam.start), cv(v1));
            _ = c._vectorSubtract(cv(v1), cv(v2), cv(&cam.target));
            toNormalIntegerVector(v1, v1);
            scratch.distance = vectorMagnitude(vv(c._vectorSubtract(cv(v2), cv(&cam.target), cv(&cam.position))));
            const from = cam.value; // read before the call, as the object does
            const ease = eased(cam, cam.ease1);
            scratch.value = from +% fixedPointMult(scratch.distance -% cam.value, ease);
            _ = c._vectorFixedPointMult(cv(v1), cv(v1), scratch.value);
            _ = c._vectorAdd(cv(&cam.computed), cv(&cam.target), cv(v1));
        },
        4, 5 => {
            scratch.rotation.vz = 0;
            scratch.rotation.vx = 0;
            const angle = cam.angle; // both read before the call, as the object does
            const angleDelta: c_int = cam.angleDelta;
            const turn = eased(cam, cam.ease1);
            scratch.rotation.vy = psx.conv(i16, @as(c_int, angle) +% ((angleDelta *% turn) >> 12));
            _ = c.RotMatrix_gte(psx.cptr(c.SVECTOR, rotation), psx.cptr(c.MATRIX, matrix));
            const radius = cam.value;
            const grow = eased(cam, cam.ease2);
            scratch.v1.vz = radius +% fixedPointMult(cam.delta, grow);
            scratch.v1.vx = 0;
            scratch.v1.vy = 0;
            _ = c.ApplyMatrixLV(psx.cptr(c.MATRIX, matrix), cv(v1), cv(v1));
            if (mode == 4) {
                const height = cam.aux.vy;
                const rise = eased(cam, cam.ease3);
                scratch.v1.vy = height +% fixedPointMult(cam.velocity.vy, rise);
                _ = c._vectorAdd(cv(&cam.computed), cv(&cam.start), cv(v1));
            } else {
                cam.computed.vx = cam.position.vx +% scratch.v1.vx;
                cam.computed.vz = cam.position.vz +% scratch.v1.vz;
                const height = cam.start.vy;
                const rise = eased(cam, cam.ease3);
                cam.computed.vy = height +% fixedPointMult(cam.velocity.vy, rise);
            }
        },
        8 => {
            c.func_800A190C(cam.value, cam.delta, psx.cptr(c.SVECTOR, rotation), 2);
            _ = c._vectorAdd(cv(&cam.computed), cv(sVectorToFixedPointVector(v1, rotation)), cv(&cam.velocity));
        },
        9 => _ = c._vectorAdd(cv(&cam.computed), cv(&cam.velocity), cv(&cam.target)),
        11 => c.func_800BCFB4(psx.cptr(c.D_800F4BA4_t2, cam)),
        else => {},
    }
    _ = copyVector(&cam.previousCamera, &cam.current);
    if ((cam.follow & cam.following) != 0) {
        _ = c._vectorAdd(cv(&cam.followPosition), cv(&cam.followPosition), cv(&cam.followDelta));
        const fraction = c.func_800BFEBC(1, cam.time, cam.duration);
        scratch.value = fraction;
        _ = c._vectorSubtract(cv(v1), cv(&cam.computed), cv(&cam.followPosition));
        _ = c._vectorFixedPointMult(cv(v1), cv(v1), scratch.value);
        _ = c._vectorAdd(cv(&cam.current), cv(&cam.followPosition), cv(v1));
    } else {
        _ = copyVector(&cam.current, &cam.computed);
    }
    if (cam.time == cam.duration) {
        cam.mode = 15;
        if (cam.follow != 0) {
            cam.following = 1;
            _ = copyVector(&cam.followPosition, &cam.current);
            _ = c._vectorSubtract(cv(&cam.followDelta), cv(&cam.current), cv(&cam.previousCamera));
        }
    }
}
