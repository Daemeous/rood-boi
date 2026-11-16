//! Starts the camera from its first keyframe (pc/src/game/BATTLE/BATTLE.PRG/5BF94.c).
const v = @import("views_5BF94");

/// orig: vs_effects_holdEffectCameraStartPose (pc/src/game/BATTLE/BATTLE.PRG/5BF94.c)
/// The current camera (vs_effects_effectCamera) = the first keyframe (vs_effects_cameraMotionEnds[0]), every byte.
pub fn vs_effects_holdEffectCameraStartPose() callconv(.c) void {
    v.g.vs_effects_effectCamera().* = v.g.vs_effects_cameraMotionEnds()[0];
}
