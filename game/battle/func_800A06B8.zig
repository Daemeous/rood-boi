//! Sets a unit's shadow radius expand/shrink state (src/BATTLE/BATTLE.PRG/30DB0.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_30DB0");

/// orig: func_800A06B8 (src/BATTLE/BATTLE.PRG/30DB0.c)
/// Zeroes the shadow radius state when `arg1` is 0, else stores the rate deltas from the base rates.
pub fn func_800A06B8(arg0: ?*v.D_800F4538_t, arg1: c_int) callconv(.c) void {
    const self = arg0.?;
    const a2: *v.D_800F4538_unk1864 = &self.unk1864;
    a2.unk5 = 0;
    if (arg1 == 0) {
        a2.unkC = self.shadowRadiusExpandRate;
        a2.unkE = self.shadowRadiusShrinkRate;
        a2.unk8 = 0;
    } else {
        a2.unk10 = psx.conv(i16, @as(c_int, self.shadowRadiusExpandRate) -% @as(c_int, a2.unkC));
        a2.unk12 = psx.conv(i16, @as(c_int, self.shadowRadiusShrinkRate) -% @as(c_int, a2.unkE));
        a2.unkA = psx.conv(i16, -%@as(c_int, a2.unk8));
    }
    a2.unk4 = psx.conv(u8, arg1);
    a2.unk5 = psx.conv(u8, arg1);
}
