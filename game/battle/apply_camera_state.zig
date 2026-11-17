//! The event camera's state handed to the renderer (pc/src/game/BATTLE/BATTLE.PRG/4A0A8.c).
const std = @import("std");
const psx = @import("common").psx;
const gte = @import("common").gte;
const pad = @import("common").pad;
const v = @import("views_4A0A8");
const c = @import("c_4A0A8");

/// The scratchpad block the shake offsets are built in (cameraScratch at 0x1F800088).
const v1: *v.VECTOR = pad.ptr(v.VECTOR, 0x98); // scratch.v1
const v2: *v.VECTOR = pad.ptr(v.VECTOR, 0xA8); // scratch.v2
const rotation: *v.SVECTOR = pad.ptr(v.SVECTOR, 0xD8); // ((SVECTOR*)0x1F800088)[10]
const matrix: *v.MATRIX = pad.ptr(v.MATRIX, 0xF8); // scratch.matrix

fn cv(p: *v.VECTOR) [*c]c.VECTOR {
    return psx.cptr(c.VECTOR, p);
}

fn camera() *v.vs_event_CameraState {
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

/// orig: _sVectorToFixedPointVector (pc/src/game/BATTLE/BATTLE.PRG/4A0A8.c)
/// dst = src * ONE.
fn sVectorToFixedPointVector(dst: *v.VECTOR, src: *v.SVECTOR) *v.VECTOR {
    dst.vx = @as(c_int, src.vx) * 4096; // a short times 4096: no overflow
    dst.vy = @as(c_int, src.vy) * 4096;
    dst.vz = @as(c_int, src.vz) * 4096;
    return dst;
}

/// orig: vs_battle_applyCameraState (pc/src/game/BATTLE/BATTLE.PRG/4A0A8.c)
/// Sets the camera's look-at and position (on the first frame, vs_event_skipNextFieldCameraRestore, from the default
/// spherical view, which the points take; else the points plus the unk1EC offset unless unk1E8 is
/// 0x2000), with the shake offsets turned to the view's heading, then any pending roll, unk174,
/// projection distance and clip requests (each cleared).
pub fn applyCameraState() callconv(.c) void {
    // The C's stack vectors: their pad words are never written (0 here; the setters read vx..vz)
    var lookAt = std.mem.zeroes(v.VECTOR);
    var position = std.mem.zeroes(v.VECTOR);
    if (v.g.vs_event_skipNextFieldCameraRestore().* != 0) {
        c.vs_battle_initialiseCameraFromSpherical(cv(&position), cv(&lookAt), -1, -1);
        _ = copyVector(&camera().lookAt.current, &lookAt);
        _ = copyVector(&camera().eye.current, &position);
    } else {
        _ = copyVector(&lookAt, &camera().lookAt.current);
        _ = copyVector(&position, &camera().eye.current);
        if (camera().originActorId != 0x2000) {
            _ = c._vectorAdd(cv(&lookAt), cv(&lookAt), cv(&camera().originOffset));
            _ = c._vectorAdd(cv(&position), cv(&position), cv(&camera().originOffset));
        }
    }
    if (camera().unk1A4.unk0.s16 != 0) {
        _ = sVectorToFixedPointVector(v1, @ptrCast(&camera().shakeEyeOffsetX)); // (SVECTOR*)&unk1BE: unk1BE..unk1C2
        _ = sVectorToFixedPointVector(v2, @ptrCast(&camera().shakeLookAtOffsetX)); // (SVECTOR*)&unk1C6: unk1C6..unk1CA
        rotation.vz = 0;
        rotation.vx = 0;
        rotation.vy = psx.conv(i16, c._atan2FixedPoint(lookAt.vx -% position.vx, lookAt.vz -% position.vz));
        _ = c.RotMatrix_gte(psx.cptr(c.SVECTOR, rotation), psx.cptr(c.MATRIX, matrix));
        gte.applyMatrixLV(matrix, v1, v1);
        gte.applyMatrixLV(matrix, v2, v2);
        _ = c._vectorAdd(cv(&lookAt), cv(&lookAt), cv(v1));
        _ = c._vectorAdd(cv(&position), cv(&position), cv(v2));
    }
    c.vs_battle_setCameraLookAt(cv(&lookAt));
    c.vs_battle_setCameraPosition(cv(&position));
    if (camera().unk168.hasChanged != 0) {
        c.vs_battle_setCameraRoll(camera().unk168.angle);
        camera().unk168.hasChanged = 0;
    }
    if (camera().unk174.hasChanged != 0) {
        c.vs_render_setFramebufferEffectRoll(camera().unk174.angle);
        camera().unk174.hasChanged = 0;
    }
    if (camera().projectionDistance.hasChanged != 0) {
        c.vs_battle_setProjectionDistance(camera().projectionDistance.value);
        camera().projectionDistance.hasChanged = 0;
    }
    if (camera().nearClip.hasChanged != 0) {
        c.vs_battle_setNearClip(camera().nearClip.value);
        camera().nearClip.hasChanged = 0;
    }
    if (camera().farClip.hasChanged != 0) {
        c.vs_battle_setFarClip(camera().farClip.value, v.g.D_800F4C6A().*);
        camera().farClip.hasChanged = 0;
    }
}
