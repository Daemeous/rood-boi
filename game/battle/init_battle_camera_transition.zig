//! Starts the camera transition into battle (src/BATTLE/BATTLE.PRG/146C.c).
const v = @import("views_146C");
const c = @import("c_146C");

/// orig: _initBattleCameraTransition (src/BATTLE/BATTLE.PRG/146C.c)
/// Allocates the transition once; unless one is already running (D_800F196C 7) its start is the current camera,
/// spherical camera, near clip, projection distance and far clip. Its end: near clip 4, projection 224, far clip
/// 0x2000, actor 0's facing as yaw, pitch 0, the current distance, aimed at actor 0's position (0xB4 up). Then
/// D_800F196C 7, camera mode 0, and the battle start calls (func_800CB654, func_8009E5C4 for all 16 actors, ...).
pub fn initBattleCameraTransition() callconv(.c) void {
    var target: v.VECTOR = undefined;
    var actor: v.func_8006EBF8_t = undefined;
    const transition = v.g.vs_battle_cameraTransition();
    if (transition.* == null) {
        transition.* = @ptrCast(@alignCast(c.vs_main_allocHeapR(@sizeOf(v.vs_battle_cameraTransition_t))));
    }
    if (v.g.D_800F196C().* != 7) {
        transition.*.?.fromCameraState = v.g.vs_scratch().camera;
        transition.*.?.fromSpherical = v.g.vs_battle_cameraCurrentSpherical().*;
        transition.*.?.fromNearClip = v.g.vs_main_nearClip().*;
        transition.*.?.fromProjectionDistance = v.g.vs_main_projectionDistance().*;
        transition.*.?.fromFarClip = v.g.vs_scratch().camera.farClip;
    }
    transition.*.?.toNearClip = 4;
    transition.*.?.toProjectionDistance = 224;
    transition.*.?.toFarClip = 0x2000;
    _ = c.func_800A1108(0, &actor);
    transition.*.?.toSpherical.values.yaw = actor.unk0.unk4.pad;
    transition.*.?.toSpherical.values.pitch = 0;
    transition.*.?.toSpherical.values.distance = v.g.vs_battle_cameraCurrentSpherical().values.distance;
    target.vx = @as(c_int, actor.unk0.unk4.vx) *% 4096;
    target.vy = (@as(c_int, actor.unk0.unk4.vy) -% 0xB4) *% 4096;
    target.vz = @as(c_int, actor.unk0.unk4.vz) *% 4096;
    c._computeCameraTransition(@ptrCast(&target));
    v.g.D_800F196C().* = 7;
    v.g._cameraMode().* = 0;
    c.func_800CB654(1);
    var i: c_int = 0;
    while (i < 16) : (i += 1) { // i < 16: no overflow
        c.func_8009E5C4(i);
    }
    _ = c.func_8008B4C8(0);
    c.func_8008D594(1);
    c.func_80095B70(1);
    c.func_800A0870(1);
}
