//! Spawns the effect-33 request at a source actor's position (pc/src/game/BATTLE/BATTLE.PRG/146C.c).
const std = @import("std");
const common = @import("common");
const psx = common.psx;
const v = @import("views_146C");
const c = @import("c_146C");

/// orig: func_8006F4B0 (pc/src/game/BATTLE/BATTLE.PRG/146C.c)
/// Spawns effect 33 at the source's position unless D_800F1868 bit 1 is set.
pub fn func_8006F4B0(source: ?*v.func_8006EBF8_t3) callconv(.c) void {
    const src = source.?;

    // The original leaves the request's other bytes uninitialised; they are zeroed here.
    var sp10: v.func_800CF0E8_t = std.mem.zeroes(v.func_800CF0E8_t);
    sp10.effectId = 33;
    sp10.unk2 = 1;
    sp10.unk4 = 4;
    sp10.unk3 = 0;
    sp10.unk8.u8[0] = 0;
    sp10.unk10 = 5;
    sp10.unk14.u16 = psx.conv(u16, (@as(c_int, psx.getBits(&src.packedTile.fields, "unk0_0")) << 7) +% 0x40);
    sp10.unk18 = psx.conv(i16, (@as(c_int, psx.getBits(&src.packedTile.fields, "unk0_16")) << 7) +% 0x40);
    sp10.unk16.u16 = psx.conv(u16, src.position.vy);
    if ((v.g.D_800F1868().* & 2) == 0) {
        _ = c.vs_effects_beginSecondaryEffects(psx.cptr(c.func_800CF0E8_t, &sp10), 0);
    }
}
