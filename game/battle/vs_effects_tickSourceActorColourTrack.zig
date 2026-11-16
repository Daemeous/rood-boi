//! An effect command: vs_render_transformActorColors on the script's actor at its frame (pc/src/game/BATTLE/BATTLE.PRG/5BF94.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_5BF94");
const c = @import("c_5BF94");

/// orig: vs_effects_tickSourceActorColourTrack (pc/src/game/BATTLE/BATTLE.PRG/5BF94.c)
/// At the command's frame, for a kind-4 unkC target whose vs_actors_actorStateFlags has bit 1: vs_render_transformActorColors; then the next command; else 1.
pub fn vs_effects_tickSourceActorColourTrack(script_arg: ?*v.D_800F53B8_t, cmd_arg: ?*v.func_800D5780_t) callconv(.c) c_int {
    const script = script_arg.?;
    const cmd = cmd_arg.?;
    const entry: *v.func_800D6310_t = @ptrCast(psx.at(@as([*]u32, @ptrCast(psx.get(cmd.keys).?)), @as(c_int, cmd.keyIndex) *% 2));
    if (entry.unk0 == psx.get(script.context.unk30).?.frame) {
        if (script.context.unkC.unk0 == 4 and (c.vs_actors_actorStateFlags(@as(c_int, psx.conv(u8, script.context.unkC.unk4.unk0))) & 2) != 0) {
            c.vs_render_transformActorColors(entry.unk7, entry.unk2, @as(c_int, psx.conv(u8, script.context.unkC.unk4.unk0)), psx.conv(c_short, @as(c_int, entry.unk4) - 0x80), psx.conv(c_short, @as(c_int, entry.unk5) - 0x80), psx.conv(c_short, @as(c_int, entry.unk6) - 0x80));
        }
        return c.vs_effects_advanceTimelineKey(@ptrCast(cmd));
    }
    return 1;
}
