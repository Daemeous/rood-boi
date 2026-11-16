//! Steps the camera shakes (pc/src/game/BATTLE/BATTLE.PRG/5BF94.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_5BF94");
const c = @import("c_5BF94");

/// orig: vs_effects_updateCameraShakeChannels (pc/src/game/BATTLE/BATTLE.PRG/5BF94.c)
/// For each of the four active shakes (vs_effects_enabledShakeChannels's bits): its amplitude from vs_effects_sampleSegmentedEffectCurve times a sine into the camera's unk20..unk2C, its phase advanced; a finished one (negative) is stopped (vs_effects_stopCameraShakeChannels).
pub fn vs_effects_updateCameraShakeChannels() callconv(.c) void {
    const shakes = v.g.vs_effects_cameraShakeChannels();
    const camera = v.g.vs_effects_effectCamera();
    var i: u5 = 0;
    while (i < 4) : (i += 1) { // i < 4
        if (((@as(c_int, v.g.vs_effects_enabledShakeChannels().*) >> i) & 1) == 0) {
            continue;
        }
        const shake = &shakes[i];
        var amount = c.vs_effects_sampleSegmentedEffectCurve(psx.getBits(shake, "unk1_0"), shake.unk0);
        if (amount >= 0) {
            amount = ((c.rsin(@as(c_int, shake.unk5) * 32) *% amount) *% shake.unk4) >> 8;
            switch (i) {
                0 => camera.unk20 = amount,
                1 => camera.unk24 = amount,
                2 => camera.unk28 = amount,
                3 => camera.unk2C = amount >> 12,
                else => unreachable,
            }
            // Object order: vs_effects_cameraShakePhaseStep, then vs_effects_randomCameraShakePhaseStep.
            const first = c.vs_effects_cameraShakePhaseStep(@ptrCast(shake));
            const second = c.vs_effects_randomCameraShakePhaseStep(@ptrCast(shake));
            shake.unk5 = psx.conv(u8, @as(c_int, shake.unk5) + first + second);
            shake.unk0 +%= 1;
        } else {
            c.vs_effects_stopCameraShakeChannels(@as(c_int, 1) << i);
        }
    }
}
