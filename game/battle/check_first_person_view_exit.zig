//! First-person view exit: returns the battle camera to its orbit
//! (pc/src/game/BATTLE/BATTLE.PRG/146C.c).
const psx = @import("common").psx;
const v = @import("views_146C");
const c = @import("c_146C");

/// orig: _checkFirstPersonViewExit (pc/src/game/BATTLE/BATTLE.PRG/146C.c)
/// Leaves first-person view on a confirm button and snaps the camera transition's yaw to a 512 step.
pub fn checkFirstPersonViewExit() callconv(.c) void {
    // PADRdown | PADj | PADstart
    if ((v.g.vs_main_buttonsPressed().all & 0xC40) == 0) return;

    v.g._cameraMode().* = 2;
    v.g._firstPersonViewEnabled().* = 0;
    v.g.D_800F19C8().* = 0;

    var sp10: v.func_8006EBF8_t = undefined;
    _ = c.vs_actors_actorPlacement(0, &sp10);
    c.vs_field_updateCameraTileConstraint(0, @ptrCast(&sp10.unk0.unk0.fields));
    _ = c.vs_battle_syncCameraAnglesFromPosition(
        psx.cptr(c._sphericalCamera, &v.g.vs_battle_cameraTransition().*.?.toSpherical),
    );

    // No call between here and the final two calls: the transition pointer is loaded once.
    const trans = v.g.vs_battle_cameraTransition().*.?;
    const yaw = trans.toSpherical.values.yaw;
    if ((yaw -% (yaw -% @rem(yaw, 512))) > 0x100) {
        trans.toSpherical.values.yaw = (yaw & ~@as(i32, 0x1FF)) +% 0x200;
    } else {
        trans.toSpherical.values.yaw = yaw & ~@as(i32, 0x1FF);
    }

    trans.fromSpherical.delta.yaw = 0;
    trans.fromSpherical.values.yaw = trans.toSpherical.values.yaw;

    trans.toSpherical = trans.fromSpherical;

    trans.toNearClip = trans.fromNearClip;
    trans.toProjectionDistance = trans.fromProjectionDistance;
    trans.toFarClip = trans.fromFarClip;
    c._computeSphericalOffset(
        psx.cptr(c.VECTOR, &v.g.vs_battle_cameraTransition().*.?.fromCameraState.position),
        psx.cptr(c.VECTOR, &v.g.vs_battle_cameraTransition().*.?.fromCameraState.lookAt),
        psx.cptr(c._sphericalValues, &v.g.vs_battle_cameraTransition().*.?.toSpherical.values),
    );
    c._computeCameraTransition(
        psx.cptr(c.VECTOR, &v.g.vs_battle_cameraTransition().*.?.fromCameraState.position),
    );
}
