//! Requests effect 54 (a damage number) (src/BATTLE/BATTLE.PRG/5BF94.c).
const std = @import("std");
const v = @import("views_5BF94");
const c = @import("c_5BF94");

/// orig: func_800CF7A8 (src/BATTLE/BATTLE.PRG/5BF94.c)
/// func_800CF3F8 of a zeroed effect-54 request with the four values packed into its second argument.
pub fn func_800CF7A8(arg0: c_int, arg1: c_int, arg2: c_int, arg3: c_int) callconv(.c) void {
    var request = std.mem.zeroes(v.func_800CF0E8_t); // the ROOD_PC branch zeroes the request
    request.effectId = 54;
    request.unk2 = 1;
    request.unk4 = 5;
    request.unk10 = 5;
    request.unk8.s16 = 0;
    request.unkA.s16 = 0;
    request.unkC = 0;
    request.unk14.u16 = 0;
    request.unk16.u16 = 0;
    request.unk18 = 0;
    request.unk3 = 0;
    _ = c.func_800CF3F8(@ptrCast(&request), (arg0 & 0xFF) | ((arg1 & 0xFF) << 8) | ((arg2 & 0xF) << 0x10) | ((arg3 & 0xF) << 0x14) | @as(c_int, @bitCast(@as(c_uint, 0xFF000000))));
}
