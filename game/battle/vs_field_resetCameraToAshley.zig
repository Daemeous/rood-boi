//! Sets up the battle camera from its spherical position and resets its angles
//! (pc/src/game/BATTLE/BATTLE.PRG/146C.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_146C");
const c = @import("c_146C");

/// orig: vs_field_resetCameraToAshley (pc/src/game/BATTLE/BATTLE.PRG/146C.c)
/// Initialises the scratchpad camera from its spherical position, sets the far clip to 0x1000 and zeroes its angles.
pub fn vs_field_resetCameraToAshley() callconv(.c) void {
    const view = v.g.vs_view();
    c.vs_battle_initialiseCameraFromSpherical(
        psx.cptr(c.VECTOR, &view.camera.position),
        psx.cptr(c.VECTOR, &view.camera.lookAt),
        -1,
        -1,
    );
    view.camera.farClip = 0x1000;
    view.camera.angles.vz = 0;
    view.camera.angles.vy = 0;
    view.camera.angles.vx = 0;
}
