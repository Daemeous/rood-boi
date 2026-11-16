//! Runs the timed effect list of an effect block: each active entry's handler,
//! clearing the entries that finish, then counting the update
//! (pc/src/game/BATTLE/BATTLE.PRG/5BF94.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_5BF94");
const c = @import("c_5BF94");

/// orig: vs_effects_tickEffectTimeline (pc/src/game/BATTLE/BATTLE.PRG/5BF94.c)
/// Runs each active entry of the effect's timeline, retires the ones that report done, then counts the update.
pub fn vs_effects_tickEffectTimeline(arg0: ?*v.D_800F53B8_t, arg1: c_int) callconv(.c) void {
    const self = arg0.?;
    var var_s1: c_int = 0;
    while (var_s1 < @as(c_int, psx.get(self.unkD1C.unk30).?.unk0)) : (var_s1 +%= 1) {
        // x86 masks a variable shift count by 31; the count is 0..31 here.
        const sh: u5 = @intCast(var_s1 & 31);
        if (((psx.get(self.unkD1C.unk30).?.unk4 >> sh) & 1) != 0) {
            const temp_a1: *v.func_800D5780_t = @alignCast(psx.at(&psx.get(self.unkD1C.unk30).?.unk8, var_s1));
            // No case for types 0 and 11: temp is uninitialised in the original; 0 here.
            const temp: c_int = switch (@as(c_int, psx.getBits(&temp_a1.unk4, "unk0_0"))) {
                1 => c.vs_effects_tickParticleEmissionTrack(@ptrCast(self), @ptrCast(temp_a1)),
                2 => c.vs_effects_tickSoundTrack(@ptrCast(self), @ptrCast(temp_a1)),
                3 => c.vs_effects_tickChildEffectTrack(@ptrCast(self), @ptrCast(temp_a1), arg1),
                4 => c.vs_effects_tickRendererTrack(@ptrCast(self), @ptrCast(temp_a1)),
                5 => c.vs_effects_tickVibrationTrack(@ptrCast(self), @ptrCast(temp_a1)),
                6 => c.vs_effects_tickSourceActorColourTrack(@ptrCast(self), @ptrCast(temp_a1)),
                7 => c.vs_effects_tickTargetActorColourTrack(@ptrCast(self), @ptrCast(temp_a1), @as(c_int, psx.get(self.unkD1C.unk30).?.unk1) & 2),
                8 => c.vs_effects_tickOtherActorColourTrack(@ptrCast(self), @ptrCast(temp_a1)),
                9 => c.vs_effects_tickSceneColourTrack(@ptrCast(self), @ptrCast(temp_a1)),
                10 => c.vs_effects_tickBackgroundColourTrack(@ptrCast(self), @ptrCast(temp_a1)),
                12 => c.vs_effects_tickActionControlTrack(@ptrCast(self), @ptrCast(temp_a1)),
                13 => c.vs_effects_tickCameraShakeTrack(@ptrCast(self), @ptrCast(temp_a1)),
                else => 0,
            };
            if (temp == 0) {
                const timeline = psx.get(self.unkD1C.unk30) orelse return;
                timeline.unk4 &= ~(@as(c_int, 1) << sh);
            }
        }
        if (psx.get(self.unkD1C.unk30) == null) return;
    }
    const timeline = psx.get(self.unkD1C.unk30).?;
    if ((timeline.unk1 & 1) == 0) {
        timeline.unk2 +%= 1;
    }
}
