//! An effect command: vs_render_transformActorColors on the request's or the script's targets (pc/src/game/BATTLE/BATTLE.PRG/5BF94.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_5BF94");
const c = @import("c_5BF94");

/// orig: vs_effects_tickTargetActorColourTrack (pc/src/game/BATTLE/BATTLE.PRG/5BF94.c)
/// At the command's frame: vs_render_transformActorColors for each of the request's kind-4 targets (or, with `all` 0, the script's unk24 target) whose vs_actors_actorStateFlags has bit 1; then the next command; else 1.
pub fn vs_effects_tickTargetActorColourTrack(script_arg: ?*v.D_800F53B8_t, cmd_arg: ?*v.func_800D5780_t, all: c_int) callconv(.c) c_int {
    const script = script_arg.?;
    const cmd = cmd_arg.?;
    const entry: *v.func_800D6310_t = @ptrCast(psx.at(@as([*]u32, @ptrCast(psx.get(cmd.unk0).?)), @as(c_int, cmd.unk6) *% 2));
    if (entry.unk0 != psx.get(script.unkD1C.unk30).?.unk2) {
        return 1;
    }
    if (all != 0) {
        const effect: *v.func_800D6048_t = @ptrCast(v.g.vs_effects_actionRequest());
        var i: c_int = 0;
        while (i < v.g.vs_effects_actionRequest().unk2) : (i += 1) { // i < a u8 count
            const target = psx.at(&effect.unk10, i); // unk10[16]: the count is not checked
            if (target.unk0 == 4 and (c.vs_actors_actorStateFlags(target.unk4.unk0 & 0xFF) & 2) != 0) {
                c.vs_render_transformActorColors(entry.unk7, entry.unk2, target.unk4.unk0 & 0xFF, psx.conv(c_short, @as(c_int, entry.unk4) - 128), psx.conv(c_short, @as(c_int, entry.unk5) - 128), psx.conv(c_short, @as(c_int, entry.unk6) - 128));
            }
        }
    } else if (script.unkD1C.unk24.unk0 == 4 and (c.vs_actors_actorStateFlags(script.unkD1C.unk24.unk4.unk0 & 0xFF) & 2) != 0) {
        c.vs_render_transformActorColors(entry.unk7, entry.unk2, script.unkD1C.unk24.unk4.unk0 & 0xFF, psx.conv(c_short, @as(c_int, entry.unk4) - 128), psx.conv(c_short, @as(c_int, entry.unk5) - 128), psx.conv(c_short, @as(c_int, entry.unk6) - 128));
    }
    return c.vs_effects_advanceTimelineKey(@ptrCast(cmd));
}
