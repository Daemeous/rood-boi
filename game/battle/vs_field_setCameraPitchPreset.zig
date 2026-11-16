//! Sets the battle camera's spherical pitch (pc/src/game/BATTLE/BATTLE.PRG/146C.c).
const v = @import("views_146C");

/// orig: vs_field_setCameraPitchPreset (pc/src/game/BATTLE/BATTLE.PRG/146C.c)
/// Sets the camera pitch to arg0 when arg0 is -0x300 or -0x80, otherwise to -0x180.
pub fn vs_field_setCameraPitchPreset(arg0: c_int) callconv(.c) void {
    const cam = v.g.vs_battle_cameraCurrentSpherical();
    if (arg0 == -0x300 or arg0 == -0x80) {
        cam.values.pitch = arg0;
        return;
    }
    cam.values.pitch = -0x180;
}
