//! The event camera's straight dolly step against the room's height (src/BATTLE/BATTLE.PRG/4A0A8.c).
const psx = @import("common").psx;
const v = @import("views_4A0A8");
const c = @import("c_4A0A8");

/// The scratchpad block the step works in (cameraMotionScratch at 0x1F800088).
const scratch: *v.cameraMotionScratch = @ptrFromInt(0x1F800088);
const delta: *v.VECTOR = @ptrFromInt(0x1F800098); // scratch.delta
const candidate: *v.VECTOR = @ptrFromInt(0x1F8000A8); // scratch.candidate
const direction: *v.VECTOR = @ptrFromInt(0x1F8000B8); // scratch.direction
const matrix: *v.MATRIX = @ptrFromInt(0x1F8000F8); // scratch.matrix

fn cv(p: *v.VECTOR) [*c]c.VECTOR {
    return psx.cptr(c.VECTOR, p);
}

fn cm(p: *v.MATRIX) [*c]c.MATRIX {
    return psx.cptr(c.MATRIX, p);
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

/// orig: _toNormalIntegerVector (src/BATTLE/BATTLE.PRG/4A0A8.c)
/// dst = the unit vector (VectorNormal) of src's integer parts.
fn toNormalIntegerVector(src: *v.VECTOR, dst: *v.VECTOR) void {
    var whole: v.VECTOR = undefined; // pad never written, as the C's
    whole.vx = src.vx >> 12;
    whole.vy = src.vy >> 12;
    whole.vz = src.vz >> 12;
    _ = c.VectorNormal(cv(&whole), cv(dst));
}

/// orig: func_800BCFB4 (src/BATTLE/BATTLE.PRG/4A0A8.c)
/// Moves `cam` toward its position in steps of `value` (2..10 of them, the last when fewer), stopping
/// below the first point whose ground (func_800BD2B8) is higher than it; done (time = duration) when
/// the move is short or ends where the last one did (unk27C).
pub fn func_800BCFB4(arg: ?*v.D_800F4BA4_t2) callconv(.c) void {
    const cam = arg.?;
    _ = c._vectorSubtract(cv(delta), cv(&cam.target), cv(&cam.position));
    c._vecToRotMatrix(cv(delta), cm(matrix));
    scratch.matrix.t[0] = cam.position.vx >> 12;
    scratch.matrix.t[1] = cam.position.vy >> 12;
    scratch.matrix.t[2] = cam.position.vz >> 12;
    scratch.delta.vx = cam.position.vx -% cam.computed.vx;
    scratch.delta.vy = c.func_800BD2B8(cm(matrix)) -% cam.computed.vy;
    scratch.delta.vz = cam.position.vz -% cam.computed.vz;
    scratch.distance = vectorMagnitude(delta);
    scratch.steps = @divTrunc(scratch.distance, cam.value);
    if (scratch.steps < 2) {
        cam.time = cam.duration;
    } else if (scratch.steps > 10) {
        scratch.steps = 10;
    }
    toNormalIntegerVector(delta, delta);
    _ = c._vectorFixedPointMult(cv(delta), cv(delta), cam.value);
    _ = copyVector(candidate, &cam.computed);
    var i: c_short = 1;
    while (i <= scratch.steps) : (i += 1) { // steps <= 10
        _ = c._vectorAdd(cv(candidate), cv(candidate), cv(delta));
        _ = c._vectorSubtract(cv(direction), cv(&cam.target), cv(candidate));
        c._vecToRotMatrix(cv(direction), cm(matrix));
        scratch.matrix.t[0] = scratch.candidate.vx >> 12;
        scratch.matrix.t[1] = scratch.candidate.vy >> 12;
        scratch.matrix.t[2] = scratch.candidate.vz >> 12;
        const height = c.func_800BD2B8(cm(matrix));
        if (height < scratch.candidate.vy) {
            scratch.delta.vx = scratch.candidate.vx -% cam.computed.vx;
            scratch.delta.vy = height -% cam.computed.vy;
            scratch.delta.vz = scratch.candidate.vz -% cam.computed.vz;
            toNormalIntegerVector(delta, delta);
            _ = c._vectorFixedPointMult(cv(delta), cv(delta), cam.value);
            break;
        }
    }
    _ = c._vectorAdd(cv(candidate), cv(delta), cv(&cam.computed));
    const last = &v.g.D_800F4BA4().*.?.unk27C;
    if (scratch.candidate.vx == last.vx and scratch.candidate.vy == last.vy and scratch.candidate.vz == last.vz) {
        cam.time = cam.duration;
    } else {
        _ = copyVector(&v.g.D_800F4BA4().*.?.unk27C, &cam.computed);
        _ = copyVector(&cam.computed, candidate);
    }
}
