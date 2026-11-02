//! Advances the fixed-point interpolation in D_800F4B88 one step and passes its
//! two outputs on (src/BATTLE/BATTLE.PRG/4A0A8.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_4A0A8");
const c = @import("c_4A0A8");

/// orig: func_800BBAFC (src/BATTLE/BATTLE.PRG/4A0A8.c)
/// Steps the interpolation in D_800F4B88, sends its two outputs on, and stops it once the last step is reached.
pub fn func_800BBAFC() callconv(.c) void {
    const s = v.g.D_800F4B88();
    if (s.unk3 != 0) {
        // unk2 is a u8 and wraps; the shift and division are done in int, and unk3 != 0 makes the division defined.
        s.unk2 +%= 1;
        const step: c_int = @divTrunc(@as(c_int, s.unk2) << 12, @as(c_int, s.unk3));
        const temp_lo: i16 = psx.conv(i16, step);
        const t: c_int = temp_lo;
        s.unk4 = psx.conv(i16, (@as(c_int, s.unk6) +% ((@as(c_int, s.unk8) *% t) >> 12)) & 0xFFF);
        s.unkA = psx.conv(i16, @as(c_int, s.unkC) +% ((@as(c_int, s.unkE) *% t) >> 12));
        s.unk10 = psx.conv(i16, @as(c_int, s.unk12) +% ((@as(c_int, s.unk14) *% t) >> 12));
        c.func_8009134C(@as(c_int, s.unk4), @as(c_int, s.unkA));
        c.func_80091320(@as(c_int, s.unk10));
        if (s.unk2 == s.unk3) {
            s.unk3 = 0;
        }
    }
}
