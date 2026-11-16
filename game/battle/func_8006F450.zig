//! Requests effect 36 from the effect system, built from the request `arg0`
//! (pc/src/game/BATTLE/BATTLE.PRG/146C.c).
const std = @import("std");
const psx = @import("common").psx;
const v = @import("views_146C");
const c = @import("c_146C");

/// orig: func_8006F450 (pc/src/game/BATTLE/BATTLE.PRG/146C.c)
/// Builds an effect request for effect 36 from `arg0` and hands it to vs_effects_beginSecondaryEffects.
pub fn func_8006F450(arg0: ?*v.func_800CF0E8_t) callconv(.c) void {
    // ROOD_PC: the whole request is zeroed first, as the compiled object does.
    var sp10: v.func_800CF0E8_t = std.mem.zeroes(v.func_800CF0E8_t);

    sp10.effectId = 36;
    sp10.unk2 = 1;
    sp10.unk4 = 4;
    sp10.unk3 = 0;
    sp10.unk8.u8[0] = 0;
    sp10.unk10 = 5;
    sp10.unk14.u16 = arg0.?.effectId;
    sp10.unk18 = psx.conv(i16, arg0.?.unk4);
    sp10.unk16.u16 = 0;
    _ = c.vs_effects_beginSecondaryEffects(psx.cptr(c.func_800CF0E8_t, &sp10), 0);
}
