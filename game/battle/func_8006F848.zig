//! Sends battle effect 20 to vs_effects_beginSecondaryEffects with a zeroed request (pc/src/game/BATTLE/BATTLE.PRG/146C.c).
const std = @import("std");
const common = @import("common");
const psx = common.psx;
const v = @import("views_146C");
const c = @import("c_146C");

/// orig: func_8006F848 (pc/src/game/BATTLE/BATTLE.PRG/146C.c)
/// Requests effect 20 from vs_effects_beginSecondaryEffects with the fields the C sets; the rest of the request is zero.
pub fn func_8006F848() callconv(.c) void {
    var sp10: v.func_800CF0E8_t = std.mem.zeroes(v.func_800CF0E8_t);

    sp10.effectId = 20;
    sp10.unk2 = 1;
    sp10.unk4 = 5;
    sp10.unk10 = 4;
    sp10.unk3 = 0;
    sp10.unk14.u8[0] = 0;
    sp10.unk16.u8[0] = 0xFD;
    _ = c.vs_effects_beginSecondaryEffects(psx.cptr(c.func_800CF0E8_t, &sp10), 0);
}
