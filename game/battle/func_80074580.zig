//! Battle camera update: pulls the projection-distance flags and, when the camera transition finishes, switches to the battle camera mode (src/BATTLE/BATTLE.PRG/146C.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_146C");
const c = @import("c_146C");

/// orig: func_80074580 (src/BATTLE/BATTLE.PRG/146C.c)
/// Clears flag 2 in the result of func_800A0BE0(0) when the projection distance is under 272, and finishes the camera transition.
pub fn func_80074580() callconv(.c) void {
    if (v.g.vs_main_projectionDistance().* < 272) {
        const flags: c_uint = c.func_800A0BE0(0);
        c.func_800A087C(0, psx.conv(c_int, flags & ~@as(c_uint, 2)));
    }

    if (c._stepCameraTransition() != 0) {
        v.g._cameraMode().* = 1;
        v.g._firstPersonViewEnabled().* = 1;
        v.g.D_800F19C8().* = 1;
        c.func_8008C40C();
    }
}
