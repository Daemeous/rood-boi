//! An effect command: vs_render_transformSceneColors at its frame (pc/src/game/BATTLE/BATTLE.PRG/5BF94.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_5BF94");
const c = @import("c_5BF94");

/// orig: vs_effects_tickSceneColourTrack (pc/src/game/BATTLE/BATTLE.PRG/5BF94.c)
/// At the command's frame (entry unk0 == the script's unk2): vs_render_transformSceneColors(unk7, unk2, unk4..6 - 128), then the next command; else 1.
pub fn vs_effects_tickSceneColourTrack(script: ?*v.D_800F53B8_t, cmd_arg: ?*v.func_800D5780_t) callconv(.c) c_int {
    const cmd = cmd_arg.?;
    const entry: *v.func_800D6310_t = @ptrCast(psx.at(@as([*]u32, @ptrCast(psx.get(cmd.unk0).?)), @as(c_int, cmd.unk6) *% 2));
    if (entry.unk0 == psx.get(script.?.unkD1C.unk30).?.unk2) {
        c.vs_render_transformSceneColors(entry.unk7, entry.unk2, psx.conv(c_short, @as(c_int, entry.unk4) - 0x80), psx.conv(c_short, @as(c_int, entry.unk5) - 0x80), psx.conv(c_short, @as(c_int, entry.unk6) - 0x80));
        return c.vs_effects_advanceTimelineKey(@ptrCast(cmd));
    }
    return 1;
}
