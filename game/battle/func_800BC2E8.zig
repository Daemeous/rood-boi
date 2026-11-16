//! The event camera's command (pc/src/game/BATTLE/BATTLE.PRG/4A0A8.c): one move of the look-at point or the eye.
const std = @import("std");
const psx = @import("common").psx;
const gte = @import("common").gte;
const pad = @import("common").pad;
const v = @import("views_4A0A8");
const c = @import("c_4A0A8");

/// The scratchpad block the commands work in (cameraScratch at 0x1F800088).
const scratch: *v.cameraScratch = pad.ptr(v.cameraScratch, 0x88);
const v1: *v.VECTOR = pad.ptr(v.VECTOR, 0x98); // scratch.v1
const v2: *v.VECTOR = pad.ptr(v.VECTOR, 0xA8); // scratch.v2
const rotation: *v.SVECTOR = pad.ptr(v.SVECTOR, 0xD8); // scratch.rotation
const rotation2: *v.SVECTOR = pad.ptr(v.SVECTOR, 0xE0); // scratch.rotation2
const matrix: *v.MATRIX = pad.ptr(v.MATRIX, 0xF8); // scratch.matrix

fn cv(p: *v.VECTOR) [*c]c.VECTOR {
    return psx.cptr(c.VECTOR, p);
}

fn vv(p: [*c]c.VECTOR) *v.VECTOR {
    return @ptrCast(@alignCast(p));
}

fn camera() *v.D_800F4BA4_t {
    return v.g.vs_event_camera().*.?;
}

/// orig: _copyVector (pc/src/game/BATTLE/BATTLE.PRG/4A0A8.c)
/// dst = src (vx, vy, vz).
fn copyVector(dst: *v.VECTOR, src: *v.VECTOR) *v.VECTOR {
    dst.vx = src.vx;
    dst.vy = src.vy;
    dst.vz = src.vz;
    return dst;
}

/// orig: _vectorMagnitude (pc/src/game/BATTLE/BATTLE.PRG/4A0A8.c)
/// The length of a 20.12 vector, from its integer parts, back in 20.12.
fn vectorMagnitude(p: *v.VECTOR) c_int {
    var copy: v.VECTOR = undefined;
    _ = copyVector(&copy, p);
    const x = (copy.vx >> 12) *% (copy.vx >> 12);
    const y = (copy.vy >> 12) *% (copy.vy >> 12);
    const z = (copy.vz >> 12) *% (copy.vz >> 12);
    return c.vs_gte_rsqrt(x +% y +% z) *% 4096;
}

/// orig: _sVectorToFixedPointVector (pc/src/game/BATTLE/BATTLE.PRG/4A0A8.c)
/// dst = src * ONE.
fn sVectorToFixedPointVector(dst: *v.VECTOR, src: *v.SVECTOR) *v.VECTOR {
    dst.vx = @as(c_int, src.vx) * 4096; // a short times 4096: no overflow
    dst.vy = @as(c_int, src.vy) * 4096;
    dst.vz = @as(c_int, src.vz) * 4096;
    return dst;
}

/// orig: func_800BC2E8 (pc/src/game/BATTLE/BATTLE.PRG/4A0A8.c)
/// Runs camera command script[0] & 15 on the look-at point (script[0] & 16 clear) or the eye: set a
/// position, snap, linear and eased moves, an orbit around the target (4, 5), an actor-relative
/// position (7, 10), follow (8), a return (9) and a straight dolly (11).
pub fn func_800BC2E8(script: [*c]u8, _: c_short) callconv(.c) c_int {
    const cam = camera();
    const cur: *v.D_800F4BA4_t2 = if ((script[0] & 16) == 0) &cam.lookAt else &cam.eye;
    const other: *v.D_800F4BA4_t2 = if ((script[0] & 16) == 0) &cam.eye else &cam.lookAt;
    switch (script[0] & 15) {
        0 => {
            _ = c._setVectorImm(cv(&cur.position), script + 1);
            return 0;
        },
        1 => {
            cur.mode = 15;
            _ = copyVector(&cur.current, &cur.position);
            _ = copyVector(&other.target, &cur.position);
            return 0;
        },
        2 => {
            cur.mode = 2;
            cur.time = 0;
            cur.ease1 = script[1];
            cur.duration = script[2];
            _ = copyVector(&cur.start, &cur.current);
            _ = c._vectorSubtract(cv(&cur.velocity), cv(&cur.position), cv(&cur.current));
            return 0;
        },
        3 => {
            cur.mode = 3;
            cur.time = 0;
            cur.ease1 = script[1];
            cur.duration = script[2];
            _ = copyVector(&cur.start, &cur.current);
            _ = c._vectorSubtract(cv(&cur.velocity), cv(&cur.position), cv(&cur.current));
            cur.value = vectorMagnitude(vv(c._vectorSubtract(cv(v1), cv(&cur.target), cv(&cur.current))));
            return 0;
        },
        4 => {
            cur.mode = 4;
            cur.time = 0;
            cur.duration = script[3];
            cur.ease1 = script[1] >> 4;
            cur.ease2 = script[2] & 15;
            cur.ease3 = script[2] >> 4;
            _ = copyVector(&cur.start, &cur.target);
            scratch.v1.vx = cur.current.vx -% cur.target.vx;
            scratch.v1.vy = 0;
            scratch.v1.vz = cur.current.vz -% cur.target.vz;
            cur.value = vectorMagnitude(v1);
            cur.angle = psx.conv(i16, c._atan2FixedPoint(scratch.v1.vx, scratch.v1.vz) & 4095);
            scratch.v2.vx = cur.position.vx -% cur.target.vx;
            scratch.v2.vy = 0;
            scratch.v2.vz = cur.position.vz -% cur.target.vz;
            cur.delta = vectorMagnitude(v2) -% cur.value;
            scratch.value = (c._atan2FixedPoint(scratch.v2.vx, scratch.v2.vz) & 4095) -% cur.angle;
            if (scratch.value < -4096 / 2) scratch.value +%= 4096;
            if (scratch.value > 4096 / 2) scratch.value -%= 4096;
            switch (script[1] & 15) {
                15 => {
                    if (scratch.value == 0) {
                        scratch.value = -4096;
                    } else if (scratch.value > 0) {
                        scratch.value -%= 4096;
                    }
                },
                1 => {
                    if (scratch.value == 0) {
                        scratch.value = 4096;
                    } else if (scratch.value < 0) {
                        scratch.value +%= 4096;
                    }
                },
                else => {},
            }
            cur.angleDelta = psx.conv(i16, scratch.value);
            cur.aux.vy = cur.current.vy -% cur.target.vy;
            cur.velocity.vy = cur.position.vy -% cur.target.vy -% cur.aux.vy;
            return 0;
        },
        5 => {
            cur.mode = 5;
            cur.time = 0;
            cur.duration = script[8];
            cur.ease1 = script[6] & 15;
            cur.ease2 = script[6] >> 4;
            cur.ease3 = script[7] & 15;
            _ = copyVector(&cur.position, &cur.target);
            scratch.v1.vx = cur.current.vx -% cur.target.vx;
            scratch.v1.vy = 0;
            scratch.v1.vz = cur.current.vz -% cur.target.vz;
            cur.value = vectorMagnitude(v1);
            cur.delta = ((cur.value *% script[5]) >> 6) -% cur.value;
            cur.angle = psx.conv(i16, c._atan2FixedPoint(scratch.v1.vx, scratch.v1.vz));
            cur.angleDelta = c.vs_battle_getShort(script + 1);
            const height: c_int = @as(c_int, c.vs_battle_getShort(script + 3)) * 4096; // a short times 4096: no overflow
            cur.start.vy = cur.current.vy;
            cur.velocity.vy = height -% cur.start.vy;
            return 0;
        },
        7 => {
            c.vs_actors_anchorPoint(c.vs_event_resolveScriptActorReference(psx.conv(u16, c.vs_battle_getShort(script + 1))), script[3], psx.cptr(c.SVECTOR, rotation), 2);
            _ = sVectorToFixedPointVector(v1, rotation);
            _ = c._setVectorImm(cv(v2), script + 4);
            _ = c._vectorAdd(cv(&cur.position), cv(v1), cv(v2));
            if (camera().originActorId != 0x2000) {
                _ = c._vectorSubtract(cv(&cur.position), cv(&cur.position), cv(&camera().originOffset));
            }
            return 0;
        },
        8 => {
            cur.mode = 8;
            cur.value = c.vs_event_resolveScriptActorReference(psx.conv(u16, c.vs_battle_getShort(script + 1)));
            cur.delta = script[3];
            cur.time = 0;
            cur.duration = script[10];
            _ = c._setVectorImm(cv(&cur.velocity), script + 4);
            return 0;
        },
        9 => {
            cur.mode = 9;
            cur.time = 0;
            cur.duration = script[1];
            _ = c._vectorSubtract(cv(&cur.velocity), cv(&cur.current), cv(&cur.target));
            return 0;
        },
        10 => {
            const id = c.vs_event_resolveScriptActorReference(psx.conv(u16, c.vs_battle_getShort(script + 1)));
            scratch.value = id;
            _ = c.vs_actors_actorPlacement(id, @ptrCast(&camera().unk1CC[1]));
            scratch.rotation2.vz = 0;
            scratch.rotation2.vx = 0;
            scratch.rotation2.vy = psx.byteAt(i16, camera(), 0x1DA).*;
            _ = c.RotMatrix_gte(psx.cptr(c.SVECTOR, rotation2), psx.cptr(c.MATRIX, matrix));
            _ = c._setVectorImm(cv(v1), script + 4);
            gte.applyMatrixLV(matrix, v1, v1);
            c.vs_actors_anchorPoint(scratch.value, script[3], psx.cptr(c.SVECTOR, rotation2), 2);
            _ = c._vectorAdd(cv(&cur.position), cv(sVectorToFixedPointVector(v2, rotation2)), cv(v1));
            if (camera().originActorId != 0x2000) {
                _ = c._vectorSubtract(cv(&cur.position), cv(&cur.position), cv(&camera().originOffset));
            }
            return 0;
        },
        11 => {
            cur.mode = 11;
            cur.time = 0;
            cur.ease1 = 0;
            cur.duration = script[2];
            _ = c._vectorSubtract(cv(v1), cv(&cur.target), cv(&cur.current));
            c._vecToRotMatrix(cv(v1), psx.cptr(c.MATRIX, matrix));
            scratch.matrix.t[0] = cur.current.vx >> 12;
            scratch.matrix.t[1] = cur.current.vy >> 12;
            scratch.matrix.t[2] = cur.current.vz >> 12;
            cur.current.vy = c.func_800BD2B8(psx.cptr(c.MATRIX, matrix));
            _ = copyVector(&cur.computed, &cur.current);
            cur.delta = vectorMagnitude(vv(c._vectorSubtract(cv(v1), cv(&cur.position), cv(&cur.computed))));
            cur.value = @divTrunc(cur.delta, @as(c_int, script[2]));
            const far: i32 = std.math.minInt(i32);
            camera().lastTerrainPosition.vz = far;
            camera().lastTerrainPosition.vy = far;
            camera().lastTerrainPosition.vx = far;
            return 0;
        },
        // 6 and 12..15 return no value in the C: both objects leave script[0] & 15 in the result register.
        else => return script[0] & 15,
    }
}
