//! Sets up a room door section from the script bytes at `arg0`
//! (src/BATTLE/BATTLE.PRG/4A0A8.c).
const std = @import("std");
const common = @import("common");
const psx = common.psx;
const v = @import("views_4A0A8");
const c = @import("c_4A0A8");

/// orig: func_800B98C0 (src/BATTLE/BATTLE.PRG/4A0A8.c)
/// Builds a room door section from its script bytes and runs the door setup; returns 0 once the state completes, else 1.
pub fn func_800B98C0(arg0: [*c]u8, _: c_short) callconv(.c) c_int {
    // uninitialised in the original (stack); zeroed here.
    var sp10: v._mpdRoomDoorSection_t = std.mem.zeroes(v._mpdRoomDoorSection_t);
    const state = v.g.D_800F4BE2();

    if (state.* == 0) {
        sp10.zndId = arg0[1];
        sp10.mpdId = arg0[2];
        psx.setBits(&sp10, "unk4_8", psx.conv(u5, arg0[3]));
        psx.setBits(&sp10.unk2, "unk0_13", psx.conv(u3, arg0[4]));
        psx.setBits(&sp10, "unk6_10", psx.conv(u5, arg0[5]));
        psx.setBits(&sp10, "unk4_13", psx.conv(u2, arg0[6]));
        psx.setBits(&sp10, "unk4_15", psx.conv(u1, arg0[7]));
        psx.setBits(&sp10, "unk6_15", psx.conv(u1, @as(c_int, arg0[7]) >> 1));

        if (arg0[1] == 0) {
            c.func_8007C580(@ptrCast(&sp10), @as(c_int, arg0[8]), @as(c_int, arg0[9]));
        } else {
            _ = c.func_8007BE10((@as(c_int, arg0[7]) >> 3) & 1);
            c.func_8007C654(@ptrCast(&sp10), @as(c_int, arg0[8]), @as(c_int, arg0[9]));
        }
        state.* = if ((@as(c_int, arg0[7]) & 4) != 0) 1 else 3;
    }

    if (state.* == 2) {
        state.* = 0;
        return 0;
    }
    return 1;
}
