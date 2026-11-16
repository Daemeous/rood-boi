//! Zeroes four battle globals and clears the first byte of each vs_effects_cameraShakeChannels entry (pc/src/game/BATTLE/BATTLE.PRG/5BF94.c).
const v = @import("views_5BF94");

/// orig: vs_effects_resetEffectCameraMotion (pc/src/game/BATTLE/BATTLE.PRG/5BF94.c)
/// Resets four battle globals to 0 and sets the unk0 byte of each of the four vs_effects_cameraShakeChannels entries to 0xFF.
pub fn vs_effects_resetEffectCameraMotion() callconv(.c) void {
    v.g.vs_effects_enabledShakeChannels().* = 0;
    v.g.D_800F55E8().* = 0;
    v.g.D_800F54D0().* = 0;
    v.g.D_800F55A0().* = 0;

    const entries = v.g.vs_effects_cameraShakeChannels();
    var i: c_int = 0;
    while (i < 4) : (i +%= 1) {
        // i is 0..3 by the loop test, so the index is in range for the entries.
        entries[@intCast(i)].unk0 = 0xFF;
    }
}
