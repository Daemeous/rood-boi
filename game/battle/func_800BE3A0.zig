//! Camera setup: resets the eye and look-at modes and clears their follow flags
//! (pc/src/game/BATTLE/BATTLE.PRG/4A0A8.c).
const v = @import("views_4A0A8");

/// orig: func_800BE3A0 (pc/src/game/BATTLE/BATTLE.PRG/4A0A8.c)
/// Sets the eye and look-at camera modes to 0xF and clears their follow flags.
pub fn func_800BE3A0() callconv(.c) void {
    const c1: *v.vs_event_CameraState = v.g.vs_event_camera().*.?;
    c1.eye.mode = 0xF;
    c1.lookAt.mode = 0xF;
    const c2: *v.vs_event_CameraState = v.g.vs_event_camera().*.?;
    c2.eye.following = 0;
    c2.eye.follow = 0;
    c2.lookAt.following = 0;
    c2.lookAt.follow = 0;
}
