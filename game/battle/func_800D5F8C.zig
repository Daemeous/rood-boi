//! An effect command: func_8007B29C on the script's actor at its frame (src/BATTLE/BATTLE.PRG/5BF94.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_5BF94");
const c = @import("c_5BF94");

/// orig: func_800D5F8C (src/BATTLE/BATTLE.PRG/5BF94.c)
/// At the command's frame, for a kind-4 unkC target whose func_800A0BE0 has bit 1: func_8007B29C; then the next command; else 1.
pub fn func_800D5F8C(script_arg: ?*v.D_800F53B8_t, cmd_arg: ?*v.func_800D5780_t) callconv(.c) c_int {
    const script = script_arg.?;
    const cmd = cmd_arg.?;
    const entry: *v.func_800D6310_t = @ptrCast(psx.at(@as([*]u32, @ptrCast(psx.get(cmd.unk0).?)), @as(c_int, cmd.unk6) *% 2));
    if (entry.unk0 == psx.get(script.unkD1C.unk30).?.unk2) {
        if (script.unkD1C.unkC.unk0 == 4 and (c.func_800A0BE0(@as(c_int, psx.conv(u8, script.unkD1C.unkC.unk4.unk0))) & 2) != 0) {
            c.func_8007B29C(entry.unk7, entry.unk2, @as(c_int, psx.conv(u8, script.unkD1C.unkC.unk4.unk0)), psx.conv(c_short, @as(c_int, entry.unk4) - 0x80), psx.conv(c_short, @as(c_int, entry.unk5) - 0x80), psx.conv(c_short, @as(c_int, entry.unk6) - 0x80));
        }
        return c.func_800D5780(@ptrCast(cmd));
    }
    return 1;
}
