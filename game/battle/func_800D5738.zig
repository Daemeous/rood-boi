//! Samples an effect command's curve (src/BATTLE/BATTLE.PRG/5BF94.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_5BF94");
const c = @import("c_5BF94");

/// orig: func_800D5738 (src/BATTLE/BATTLE.PRG/5BF94.c)
/// unkA = func_800D12D8(the unk0_4 field of the command word after the current pair), as an i16.
pub fn func_800D5738(command: ?*v.func_800D5780_t) callconv(.c) void {
    const cmd = command.?;
    const words: [*]u32 = @ptrCast(psx.get(cmd.unk0).?);
    const entry: *v.func_800D57FC_t2 = @ptrCast(psx.at(words, @as(c_int, cmd.unk6) *% 2 +% 1));
    cmd.unkA = psx.conv(i16, c.func_800D12D8(@as(c_int, psx.getBits(entry, "unk0_4"))));
}
