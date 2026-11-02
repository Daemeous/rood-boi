//! The event camera's state handed to the renderer (src/BATTLE/BATTLE.PRG/4A0A8.c).
const std = @import("std");
const psx = @import("common").psx;
const v = @import("views_4A0A8");
const c = @import("c_4A0A8");

/// The scratchpad block the shake offsets are built in (cameraScratch at 0x1F800088).
const v1: *v.VECTOR = @ptrFromInt(0x1F800098); // scratch.v1
const v2: *v.VECTOR = @ptrFromInt(0x1F8000A8); // scratch.v2
const rotation: *v.SVECTOR = @ptrFromInt(0x1F8000D8); // ((SVECTOR*)0x1F800088)[10]
const matrix: *v.MATRIX = @ptrFromInt(0x1F8000F8); // scratch.matrix

fn cv(p: *v.VECTOR) [*c]c.VECTOR {
    return psx.cptr(c.VECTOR, p);
}

fn camera() *v.D_800F4BA4_t {
    return v.g.D_800F4BA4().*.?;
}

/// orig: _copyVector (src/BATTLE/BATTLE.PRG/4A0A8.c)
/// dst = src (vx, vy, vz).
fn copyVector(dst: *v.VECTOR, src: *v.VECTOR) *v.VECTOR {
    dst.vx = src.vx;
    dst.vy = src.vy;
    dst.vz = src.vz;
    return dst;
}

/// orig: _sVectorToFixedPointVector (src/BATTLE/BATTLE.PRG/4A0A8.c)
/// dst = src * ONE.
fn sVectorToFixedPointVector(dst: *v.VECTOR, src: *v.SVECTOR) *v.VECTOR {
    dst.vx = @as(c_int, src.vx) * 4096; // a short times 4096: no overflow
    dst.vy = @as(c_int, src.vy) * 4096;
    dst.vz = @as(c_int, src.vz) * 4096;
    return dst;
}

/// orig: vs_battle_applyCameraState (src/BATTLE/BATTLE.PRG/4A0A8.c)
/// Sets the camera's look-at and position (on the first frame, D_800F4BA0, from the default
/// spherical view, which the points take; else the points plus the unk1EC offset unless unk1E8 is
/// 0x2000), with the shake offsets turned to the view's heading, then any pending roll, unk174,
/// projection distance and clip requests (each cleared).
pub fn applyCameraState() callconv(.c) void {
    // The C's stack vectors: their pad words are never written (0 here; the setters read vx..vz)
    var lookAt = std.mem.zeroes(v.VECTOR);
    var position = std.mem.zeroes(v.VECTOR);
    if (v.g.D_800F4BA0().* != 0) {
        c.vs_battle_initialiseCameraFromSpherical(cv(&position), cv(&lookAt), -1, -1);
        _ = copyVector(&camera().lookAt.current, &lookAt);
        _ = copyVector(&camera().eye.current, &position);
    } else {
        _ = copyVector(&lookAt, &camera().lookAt.current);
        _ = copyVector(&position, &camera().eye.current);
        if (camera().unk1E8 != 0x2000) {
            _ = c._vectorAdd(cv(&lookAt), cv(&lookAt), cv(&camera().unk1EC));
            _ = c._vectorAdd(cv(&position), cv(&position), cv(&camera().unk1EC));
        }
    }
    if (camera().unk1A4.unk0.s16 != 0) {
        _ = sVectorToFixedPointVector(v1, @ptrCast(&camera().unk1BE)); // (SVECTOR*)&unk1BE: unk1BE..unk1C2
        _ = sVectorToFixedPointVector(v2, @ptrCast(&camera().unk1C6)); // (SVECTOR*)&unk1C6: unk1C6..unk1CA
        rotation.vz = 0;
        rotation.vx = 0;
        rotation.vy = psx.conv(i16, c._atan2FixedPoint(lookAt.vx -% position.vx, lookAt.vz -% position.vz));
        _ = c.RotMatrix_gte(psx.cptr(c.SVECTOR, rotation), psx.cptr(c.MATRIX, matrix));
        _ = c.ApplyMatrixLV(psx.cptr(c.MATRIX, matrix), cv(v1), cv(v1));
        _ = c.ApplyMatrixLV(psx.cptr(c.MATRIX, matrix), cv(v2), cv(v2));
        _ = c._vectorAdd(cv(&lookAt), cv(&lookAt), cv(v1));
        _ = c._vectorAdd(cv(&position), cv(&position), cv(v2));
    }
    c.vs_battle_setCameraLookAt(cv(&lookAt));
    c.vs_battle_setCameraPosition(cv(&position));
    if (camera().unk168.unk0 != 0) {
        c.vs_battle_setCameraRoll(camera().unk168.unk2);
        camera().unk168.unk0 = 0;
    }
    if (camera().unk174.unk0 != 0) {
        c.func_8007DDAC(camera().unk174.unk2);
        camera().unk174.unk0 = 0;
    }
    if (camera().projectionDistance.unk0 != 0) {
        c.vs_battle_setProjectionDistance(camera().projectionDistance.unk2);
        camera().projectionDistance.unk0 = 0;
    }
    if (camera().nearClip.unk0 != 0) {
        c.vs_battle_setNearClip(camera().nearClip.unk2);
        camera().nearClip.unk0 = 0;
    }
    if (camera().farClip.unk0 != 0) {
        c.vs_battle_setFarClip(camera().farClip.unk2, v.g.D_800F4C6A().*);
        camera().farClip.unk0 = 0;
    }
}
