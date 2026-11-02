//! An effect command: starts an effect renderer at its frame (src/BATTLE/BATTLE.PRG/5BF94.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_5BF94");
const c = @import("c_5BF94");

/// orig: func_800D5D74 (src/BATTLE/BATTLE.PRG/5BF94.c)
/// At the command's frame: func_800CF694(script, the renderer it names, its parameters), then the next command; else 1.
pub fn func_800D5D74(script: ?*v.D_800F53B8_t, cmd_arg: ?*v.func_800D5780_t) callconv(.c) c_int {
    const cmd = cmd_arg.?;
    // The entry's guest address in 32-bit arithmetic, as the C computes it.
    const entry: *align(1) v.func_800D5D74_t = @ptrFromInt(psx.conv(u32, @as(c_int, cmd.unk6) *% 8) +% psx.rawOf(cmd.unk0));
    if (@as(c_int, psx.getBits(entry, "unk0_0")) == psx.get(script.?.unkD1C.unk30).?.unk2) {
        const renderer = v.g.vs_battle_effectRenderers()[psx.getBits(entry, "effectRenderer")];
        const params = (@as(c_int, psx.getBits(entry, "unk0_9")) +% @as(c_int, psx.getBits(entry, "unk0_21")) *% 256) | (@as(c_int, entry.unk4) << 0x10);
        _ = c.func_800CF694(@ptrCast(script), @ptrCast(renderer), params);
        return c.func_800D5780(@ptrCast(cmd));
    }
    return 1;
}
