//! Requests effect 56 (src/BATTLE/BATTLE.PRG/5BF94.c).
const std = @import("std");
const v = @import("views_5BF94");
const c = @import("c_5BF94");

/// orig: func_800CF8BC (src/BATTLE/BATTLE.PRG/5BF94.c)
/// func_800CF3F8 of a zeroed effect-56 request (the ROOD_PC zeroing) with D_800F5238 in unk8's first byte.
pub fn func_800CF8BC() callconv(.c) void {
    var request = std.mem.zeroes(v.func_800CF0E8_t); // the ROOD_PC branch zeroes the request
    request.effectId = 56;
    request.unk2 = 1;
    request.unk4 = 4;
    request.unkA.u8[0] = 0;
    request.unk10 = 5;
    request.unk14.u16 = 0;
    request.unk16.u16 = 0;
    request.unk18 = 0;
    request.unk3 = 0;
    request.unk8.u8[0] = v.g.D_800F5238().*;
    _ = c.func_800CF3F8(@ptrCast(&request), 0);
}
