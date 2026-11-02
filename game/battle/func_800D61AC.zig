//! An effect command: func_8007B29C on every effect target at its frame (src/BATTLE/BATTLE.PRG/5BF94.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_5BF94");
const c = @import("c_5BF94");

/// orig: func_800D61AC (src/BATTLE/BATTLE.PRG/5BF94.c)
/// At the command's frame: func_8007B29C for each of D_800F5230's unk3 targets whose func_800A0BE0 has bit 1, then the next command; else 1.
pub fn func_800D61AC(script: ?*v.D_800F53B8_t, cmd_arg: ?*v.func_800D5780_t) callconv(.c) c_int {
    const cmd = cmd_arg.?;
    const entry: *v.func_800D6310_t = @ptrCast(psx.at(@as([*]u32, @ptrCast(psx.get(cmd.unk0).?)), @as(c_int, cmd.unk6) *% 2));
    if (entry.unk0 == psx.get(script.?.unkD1C.unk30).?.unk2) {
        const request = v.g.D_800F5230();
        var i: c_int = 0;
        while (i < request.unk3) : (i += 1) { // i < a u8 count
            const target: c_int = psx.at(&request.unkD0, i).*; // unkD0[16]: the count is not checked
            if ((c.func_800A0BE0(target) & 2) != 0) {
                c.func_8007B29C(entry.unk7, entry.unk2, psx.at(&request.unkD0, i).*, psx.conv(c_short, @as(c_int, entry.unk4) - 0x80), psx.conv(c_short, @as(c_int, entry.unk5) - 0x80), psx.conv(c_short, @as(c_int, entry.unk6) - 0x80));
            }
        }
        return c.func_800D5780(@ptrCast(cmd));
    }
    return 1;
}
