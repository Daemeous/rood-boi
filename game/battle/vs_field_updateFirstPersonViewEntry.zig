//! Battle camera update: pulls the projection-distance flags and, when the camera transition finishes, switches to the battle camera mode (pc/src/game/BATTLE/BATTLE.PRG/146C.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_146C");
const c = @import("c_146C");

/// orig: vs_field_updateFirstPersonViewEntry (pc/src/game/BATTLE/BATTLE.PRG/146C.c)
/// Clears flag 2 in the result of vs_actors_actorStateFlags(0) when the projection distance is under 272, and finishes the camera transition.
pub fn vs_field_updateFirstPersonViewEntry() callconv(.c) void {
    if (v.g.vs_main_projectionDistance().* < 272) {
        const flags: c_uint = c.vs_actors_actorStateFlags(0);
        c.vs_actors_setActorDisplayFlags(0, psx.conv(c_int, flags & ~@as(c_uint, 2)));
    }

    if (c._stepCameraTransition() != 0) {
        v.g._cameraMode().* = 1;
        v.g._firstPersonViewEnabled().* = 1;
        v.g.D_800F19C8().* = 1;
        c.vs_field_clearGeometryOcclusion();
    }
}
