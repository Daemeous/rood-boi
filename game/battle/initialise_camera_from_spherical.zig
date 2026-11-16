//! Points the battle camera along its spherical values and places the look-at and position vectors
//! (pc/src/game/BATTLE/BATTLE.PRG/146C.c).
const psx = @import("common").psx;
const v = @import("views_146C");
const c = @import("c_146C");

/// orig: vs_battle_initialiseCameraFromSpherical (pc/src/game/BATTLE/BATTLE.PRG/146C.c)
/// Sets the camera's facing and distance for `mode`, then writes the look-at point and the spherical offset position.
pub fn initialiseCameraFromSpherical(outputPos: ?*v.VECTOR, outputLookAt: ?*v.VECTOR, facing: c_int, mode: c_int) callconv(.c) void {
    const camera = v.g.vs_battle_cameraCurrentSpherical();

    if (facing != -1) {
        var varV0: c_int = facing;
        if (facing < 0) varV0 = facing +% 7;
        const sector: c_int = facing -% ((varV0 >> 3) *% 8);
        const yaw: c_int = sector << 9;
        camera.delta.pitch = yaw;
        camera.values.yaw = yaw;
    }

    if (mode == 1) {
        camera.initialDistance = 0x600;
        camera.values.distance = 0x600;
        camera.delta.mode = 0;
    } else if (mode == 2) {
        camera.initialDistance = 0x900;
        camera.values.distance = 0x900;
        camera.delta.mode = mode;
    }

    var sp10: v.func_8006EBF8_t = undefined;
    _ = c.vs_actors_actorPlacement(0, &sp10);

    const lookAt = outputLookAt.?;
    lookAt.vx = @as(c_int, sp10.location.position.vx) << 12;
    lookAt.vy = psx.conv(c_int, psx.conv(c_uint, @as(c_int, sp10.location.position.vy) << 12) +% 0xFFFA6000);
    lookAt.vz = @as(c_int, sp10.location.position.vz) << 12;

    c._clampPositionToZoneBounds(psx.cptr(c.VECTOR, lookAt), psx.cptr(c.VECTOR, lookAt));

    const posPtr: [*c]c.VECTOR = if (outputPos) |p| psx.cptr(c.VECTOR, p) else null;
    c._computeSphericalOffset(posPtr, psx.cptr(c.VECTOR, lookAt), psx.cptr(c._sphericalValues, &camera.values));
}
