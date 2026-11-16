//! Starts a screen transition: records its effect, speed and wipe angle (pc/src/game/BATTLE/BATTLE.PRG/146C.c).
const v = @import("views_146C");

/// orig: vs_field_beginScreenFadeOut (pc/src/game/BATTLE/BATTLE.PRG/146C.c)
/// Starts a screen transition with the given effect, speed and wipe angle, unless one is already in its first two steps.
pub fn vs_field_beginScreenFadeOut(effect: c_int, speed: c_int, angle: c_int) callconv(.c) void {
    const step = v.g.vs_battle_screenTransitionStep();
    if (step.* == 1 or step.* == 2) {
        return;
    }

    step.* = 5;
    v.g.vs_battle_screenTransitionEffect().* = effect;
    v.g.vs_battle_screenTransitionSpeed().* = speed;
    v.g.vs_battle_screenTransitionWipeAngle().* = angle;
}
