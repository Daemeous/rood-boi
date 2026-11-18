//! An effect command: starts an effect renderer at its frame (pc/src/game/BATTLE/BATTLE.PRG/5BF94.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_5BF94");
const c = @import("c_5BF94");

/// orig: vs_effects_tickRendererTrack (pc/src/game/BATTLE/BATTLE.PRG/5BF94.c)
/// At the command's frame: vs_effects_attachEffectRenderer(script, the renderer it names, its parameters), then the next command; else 1.
pub fn vs_effects_tickRendererTrack(script: ?*v.vs_effects_EffectInstance, cmd_arg: ?*v.func_800D5780_t) callconv(.c) c_int {
    const cmd = cmd_arg.?;
    // The entry's address: the keys plus the key index times 8.
    const entry: *align(1) v.func_800D5D74_t = @ptrFromInt(@as(usize, psx.conv(u32, @as(c_int, cmd.keyIndex) *% 8)) +% @intFromPtr(psx.get(cmd.keys)));
    if (@as(c_int, psx.getBits(entry, "unk0_0")) == psx.get(script.?.context.unk30).?.frame) {
        const renderer = v.g.vs_battle_effectRenderers()[psx.getBits(entry, "effectRenderer")];
        const params = (@as(c_int, psx.getBits(entry, "unk0_9")) +% @as(c_int, psx.getBits(entry, "unk0_21")) *% 256) | (@as(c_int, entry.unk4) << 0x10);
        _ = c.vs_effects_attachEffectRenderer(@ptrCast(script), @ptrCast(renderer), params);
        return c.vs_effects_advanceTimelineKey(@ptrCast(cmd));
    }
    return 1;
}
