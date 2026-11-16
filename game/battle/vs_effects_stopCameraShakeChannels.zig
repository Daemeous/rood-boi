//! Clears the vs_effects_cameraShakeChannels slot and runs vs_effects_disableCameraShakeChannels for each bit set in its argument (pc/src/game/BATTLE/BATTLE.PRG/5BF94.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_5BF94");
const c = @import("c_5BF94");

/// orig: vs_effects_stopCameraShakeChannels (pc/src/game/BATTLE/BATTLE.PRG/5BF94.c)
/// For each bit i of `arg0` (0..3) set: marks slot i of vs_effects_cameraShakeChannels with 0xFF and calls vs_effects_disableCameraShakeChannels(1 << i).
pub fn vs_effects_stopCameraShakeChannels(arg0: c_int) callconv(.c) void {
    // i runs 0..3 (i < 4 bounds it), so the increment cannot overflow.
    var i: c_int = 0;
    while (i < 4) : (i += 1) {
        if (((arg0 >> @intCast(i)) & 1) != 0) {
            v.g.vs_effects_cameraShakeChannels()[@intCast(i)].unk0 = 0xFF;
            c.vs_effects_disableCameraShakeChannels(@as(c_int, 1) << @intCast(i));
        }
    }
}
