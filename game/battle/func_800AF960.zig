//! Steps two per-slot counters of the 44F14 unit's state block toward their targets (pc/src/game/BATTLE/BATTLE.PRG/44F14.c).
const psx = @import("common").psx;
const v = @import("views_44F14");

/// orig: func_800AF960 (pc/src/game/BATTLE/BATTLE.PRG/44F14.c)
/// Moves a fraction of the remaining value of two pairs of counters at `arg0.unk1864` each frame, and masks the angle at unk8 to 12 bits.
pub fn func_800AF960(arg0: ?*v.D_800F45E0_t) callconv(.c) void {
    const a2 = &arg0.?.unk1864;

    if (a2.unk4 != 0) {
        const temp_lo: c_int = @divTrunc(@as(c_int, a2.unkA), @as(c_int, a2.unk4));
        a2.unkA = psx.conv(i16, @as(c_int, a2.unkA) -% temp_lo);
        a2.unk8 = psx.conv(u16, @as(c_int, a2.unk8) +% temp_lo);
        a2.unk4 -%= 1;
    }

    a2.unk8 &= 0xFFF;

    if (a2.unk5 != 0) {
        var temp_lo: c_int = @divTrunc(@as(c_int, a2.radiusExpandDelta), @as(c_int, a2.unk5));
        a2.radiusExpandDelta = psx.conv(i16, @as(c_int, a2.radiusExpandDelta) -% temp_lo);
        a2.radiusExpandRate = psx.conv(u16, @as(c_int, a2.radiusExpandRate) +% temp_lo);
        temp_lo = @divTrunc(@as(c_int, a2.radiusShrinkDelta), @as(c_int, a2.unk5));
        a2.radiusShrinkDelta = psx.conv(i16, @as(c_int, a2.radiusShrinkDelta) -% temp_lo);
        a2.radiusShrinkRate = psx.conv(u16, @as(c_int, a2.radiusShrinkRate) +% temp_lo);
        a2.unk5 -%= 1;
    }
}
