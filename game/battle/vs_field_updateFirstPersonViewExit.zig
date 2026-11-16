//! Battle camera step: raises the projection-distance flag and ends a finished camera transition (pc/src/game/BATTLE/BATTLE.PRG/146C.c).
const psx = @import("common").psx;
const v = @import("views_146C");
const c = @import("c_146C");

/// orig: vs_field_updateFirstPersonViewExit (pc/src/game/BATTLE/BATTLE.PRG/146C.c)
/// Sets flag 2 of the mode word when the projection distance exceeds 272, then ends the camera transition once it has stepped to completion.
pub fn vs_field_updateFirstPersonViewExit() callconv(.c) void {
    if (v.g.vs_main_projectionDistance().* > 272) {
        c.vs_actors_setActorDisplayFlags(0, psx.conv(c_int, c.vs_actors_actorStateFlags(0) | 2));
    }
    if (c._stepCameraTransition() != 0) {
        c._endBattleCameraTransition();
    }
}
