//! An effect-script opcode: a camera move to the saved camera (pc/src/game/BATTLE/BATTLE.PRG/5BF94.c).
const v = @import("views_5BF94");
const c = @import("c_5BF94");

/// orig: vs_effects_restoreActionEffectCamera (pc/src/game/BATTLE/BATTLE.PRG/5BF94.c)
/// Keyframe 0 from the current camera (vs_effects_captureEffectCamera), keyframe 1 = vs_effects_savedActionCamera, over `frames` (0 means 1) frames: vs_effects_setEffectCameraTransitionMode(2).
pub fn vs_effects_restoreActionEffectCamera(script: ?*v.vs_effects_EffectInstance, frames_arg: u8) callconv(.c) void {
    _ = script;
    var frames = frames_arg;
    const keys = v.g.vs_effects_cameraMotionEnds();
    c.vs_effects_captureEffectCamera(@ptrCast(&keys[0]));
    keys[1] = v.g.vs_effects_savedActionCamera().*;
    v.g.D_800F54D0().* = 0;
    if (frames == 0) {
        frames = 1;
    }
    v.g.D_800F55A0().* = frames;
    c.vs_effects_setEffectCameraTransitionMode(2);
}
