//! An effect command: func_8007B344 at its frame (src/BATTLE/BATTLE.PRG/5BF94.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_5BF94");
const c = @import("c_5BF94");

/// orig: func_800D6310 (src/BATTLE/BATTLE.PRG/5BF94.c)
/// At the command's frame (entry unk0 == the script's unk2): func_8007B344(unk7, unk2, unk4..6 - 128), then the next command; else 1.
pub fn func_800D6310(script: ?*v.D_800F53B8_t, cmd_arg: ?*v.func_800D5780_t) callconv(.c) c_int {
    const cmd = cmd_arg.?;
    const entry: *v.func_800D6310_t = @ptrCast(psx.at(@as([*]u32, @ptrCast(psx.get(cmd.unk0).?)), @as(c_int, cmd.unk6) *% 2));
    if (entry.unk0 == psx.get(script.?.unkD1C.unk30).?.unk2) {
        c.func_8007B344(entry.unk7, entry.unk2, psx.conv(c_short, @as(c_int, entry.unk4) - 0x80), psx.conv(c_short, @as(c_int, entry.unk5) - 0x80), psx.conv(c_short, @as(c_int, entry.unk6) - 0x80));
        return c.func_800D5780(@ptrCast(cmd));
    }
    return 1;
}
