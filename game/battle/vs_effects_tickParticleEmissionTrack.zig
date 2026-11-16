//! An effect command: a timed func_800D2ADC call (pc/src/game/BATTLE/BATTLE.PRG/5BF94.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_5BF94");
const c = @import("c_5BF94");

/// orig: vs_effects_tickParticleEmissionTrack (pc/src/game/BATTLE/BATTLE.PRG/5BF94.c)
/// From the command's frame on: vs_effects_sampleAllEffectCurves(unk8), func_800D2ADC with the entry's fields, unk8 + 1; unkA counts down unless the script is paused (unk1 bit 0); the next command at 0.
pub fn vs_effects_tickParticleEmissionTrack(script: ?*v.D_800F53B8_t, cmd_arg: ?*v.func_800D5780_t) callconv(.c) c_int {
    const s = script.?;
    const cmd = cmd_arg.?;
    const e: [*]v.func_800D57FC_t = @ptrCast(psx.get(cmd.unk0).?);
    var ret: c_int = 1;
    const key: c_int = psx.getBits(psx.at(e, cmd.unk6), "unk0_0");
    const cur: c_int = psx.get(s.unkD1C.unk30).?.unk2;
    if (cur < key) {
        return ret;
    }
    c.vs_effects_sampleAllEffectCurves(cmd.unk8);
    const entry = psx.at(e, cmd.unk6);
    c.func_800D2ADC(@ptrCast(s), psx.getBits(entry, "unk0_20"), psx.getBits(entry, "unk0_26"), psx.getBits(entry, "unk0_16"), null);
    cmd.unk8 +%= 1;
    if ((psx.get(s.unkD1C.unk30).?.unk1 & 1) == 0) {
        cmd.unkA -%= 1;
    }
    if (cmd.unkA == 0) {
        ret = c.vs_effects_advanceTimelineKey(@ptrCast(cmd));
        cmd.unk8 = 0;
    }
    return ret;
}
