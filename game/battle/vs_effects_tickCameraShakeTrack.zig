//! An effect command: a camera shake at its frame (pc/src/game/BATTLE/BATTLE.PRG/5BF94.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_5BF94");
const c = @import("c_5BF94");

/// orig: vs_effects_tickCameraShakeTrack (pc/src/game/BATTLE/BATTLE.PRG/5BF94.c)
/// From the command's frame on: at it, vs_effects_startCameraShakeChannels with the first entry's fields (an original bug: e->, not e[unk6]); each call counts unkA down, the next command at 0.
pub fn vs_effects_tickCameraShakeTrack(script: ?*v.vs_effects_EffectInstance, cmd_arg: ?*v.func_800D5780_t) callconv(.c) c_int {
    const cmd = cmd_arg.?;
    const e: [*]v.func_800D6A18_t = @ptrCast(psx.get(cmd.keys).?);
    var ret: c_int = 1;
    const key: c_int = psx.getBits(psx.at(e, cmd.keyIndex), "unk0_0");
    const cur: c_int = psx.get(script.?.context.unk30).?.frame;
    if (cur < key) {
        return ret;
    }
    if (key == cur) {
        // The original reads entry 0 (e->), not the current one.
        c.vs_effects_startCameraShakeChannels(psx.getBits(&e[0], "unk4_0"), psx.getBits(&e[0], "unk4_4"), psx.getBits(&e[0], "unk4_9"), psx.getBits(&e[0], "unk4_16"), psx.getBits(&e[0], "unk4_17"), psx.getBits(&e[0], "unk4_24"), psx.getBits(&e[0], "unk4_25"));
    }
    cmd.unkA -%= 1;
    if (cmd.unkA == 0) {
        ret = c.vs_effects_advanceTimelineKey(@ptrCast(cmd));
    }
    return ret;
}
