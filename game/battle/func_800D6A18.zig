//! An effect command: a camera shake at its frame (src/BATTLE/BATTLE.PRG/5BF94.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_5BF94");
const c = @import("c_5BF94");

/// orig: func_800D6A18 (src/BATTLE/BATTLE.PRG/5BF94.c)
/// From the command's frame on: at it, func_800D1EF0 with the first entry's fields (an original bug: e->, not e[unk6]); each call counts unkA down, the next command at 0.
pub fn func_800D6A18(script: ?*v.D_800F53B8_t, cmd_arg: ?*v.func_800D5780_t) callconv(.c) c_int {
    const cmd = cmd_arg.?;
    const e: [*]v.func_800D6A18_t = @ptrCast(psx.get(cmd.unk0).?);
    var ret: c_int = 1;
    const key: c_int = psx.getBits(psx.at(e, cmd.unk6), "unk0_0");
    const cur: c_int = psx.get(script.?.unkD1C.unk30).?.unk2;
    if (cur < key) {
        return ret;
    }
    if (key == cur) {
        // The original reads entry 0 (e->), not the current one.
        c.func_800D1EF0(psx.getBits(&e[0], "unk4_0"), psx.getBits(&e[0], "unk4_4"), psx.getBits(&e[0], "unk4_9"), psx.getBits(&e[0], "unk4_16"), psx.getBits(&e[0], "unk4_17"), psx.getBits(&e[0], "unk4_24"), psx.getBits(&e[0], "unk4_25"));
    }
    cmd.unkA -%= 1;
    if (cmd.unkA == 0) {
        ret = c.func_800D5780(@ptrCast(cmd));
    }
    return ret;
}
