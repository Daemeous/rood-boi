//! Battle sub-action dispatcher: runs the mode in unk0 and restores the flags byte it saved on entry (src/BATTLE/BATTLE.PRG/58578.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_58578");
const c = @import("c_58578");

/// orig: func_800C1564 (src/BATTLE/BATTLE.PRG/58578.c)
/// Runs the sub-action selected by the mode byte, then puts back the flags byte saved on entry.
pub fn func_800C1564(arg0: ?*v.func_800C1564_t, arg1: [*c]c_ushort) callconv(.c) c_int {
    const state = arg0.?;
    var saved = state.unk4.flags;
    var ret: c_int = 0;

    switch (@as(c_int, state.unk0)) {
        1 => {
            ret = c.func_800C1034(@ptrCast(state), arg1);
        },
        2 => {
            psx.setBits(&state.unk4.flags, "unk1", psx.conv(u8, @as(c_int, psx.getBits(&state.unk4.flags, "unk1")) << 1));
            ret = c.func_800C110C(@ptrCast(state), arg1, 1);
        },
        3 => {
            ret = c.func_800C110C(@ptrCast(state), arg1, 0);
        },
        4 => {
            ret = c.func_800C123C(@ptrCast(state), arg1, 0);
        },
        5 => {
            psx.setBits(&state.unk4.flags, "unk3", psx.conv(u8, @as(c_int, psx.getBits(&saved, "unk3")) +% 0x80));
            ret = c.func_800C123C(@ptrCast(state), arg1, 1);
        },
        6 => {
            ret = c.func_800C1384(@ptrCast(state), arg1, 1);
        },
        7 => {
            ret = c.func_800C1384(@ptrCast(state), arg1, 0);
        },
        else => {},
    }

    state.unk4.flags = saved;
    return ret;
}
