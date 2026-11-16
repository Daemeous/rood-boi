//! An effect command: vs_render_transformActorColors on every effect target at its frame (pc/src/game/BATTLE/BATTLE.PRG/5BF94.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_5BF94");
const c = @import("c_5BF94");

/// orig: vs_effects_tickOtherActorColourTrack (pc/src/game/BATTLE/BATTLE.PRG/5BF94.c)
/// At the command's frame: vs_render_transformActorColors for each of vs_effects_actionRequest's unk3 targets whose vs_actors_actorStateFlags has bit 1, then the next command; else 1.
pub fn vs_effects_tickOtherActorColourTrack(script: ?*v.D_800F53B8_t, cmd_arg: ?*v.func_800D5780_t) callconv(.c) c_int {
    const cmd = cmd_arg.?;
    const entry: *v.func_800D6310_t = @ptrCast(psx.at(@as([*]u32, @ptrCast(psx.get(cmd.keys).?)), @as(c_int, cmd.keyIndex) *% 2));
    if (entry.unk0 == psx.get(script.?.context.unk30).?.frame) {
        const request = v.g.vs_effects_actionRequest();
        var i: c_int = 0;
        while (i < request.unk3) : (i += 1) { // i < a u8 count
            const target: c_int = psx.at(&request.unkD0, i).*; // unkD0[16]: the count is not checked
            if ((c.vs_actors_actorStateFlags(target) & 2) != 0) {
                c.vs_render_transformActorColors(entry.unk7, entry.unk2, psx.at(&request.unkD0, i).*, psx.conv(c_short, @as(c_int, entry.unk4) - 0x80), psx.conv(c_short, @as(c_int, entry.unk5) - 0x80), psx.conv(c_short, @as(c_int, entry.unk6) - 0x80));
            }
        }
        return c.vs_effects_advanceTimelineKey(@ptrCast(cmd));
    }
    return 1;
}
