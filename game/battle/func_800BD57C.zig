//! Advances a timed value by half the game tick speed and recomputes its
//! interpolated output (src/BATTLE/BATTLE.PRG/4A0A8.c).
const psx = @import("common").psx;
const v = @import("views_4A0A8");
const c = @import("c_4A0A8");

/// orig: func_800BD57C (src/BATTLE/BATTLE.PRG/4A0A8.c)
/// While the timer's target (unkA) is positive, steps its elapsed time and sets the output from the curve.
pub fn func_800BD57C(arg0: ?*v.func_800BD57C_t) callconv(.c) void {
    const p = arg0.?;
    if (@as(c_int, p.unkA) > 0) {
        p.unk8 = psx.conv(i16, @as(c_int, p.unk8) +% (v.g.vs_gametime_tickspeed().* >> 1));

        // The call comes first; the field loads after it, as GCC orders them.
        const f = c.func_800BFEBC(@as(c_int, p.unk1), @as(c_int, p.unk8), @as(c_int, p.unkA));
        p.unk2 = psx.conv(i16, @as(c_int, p.unk4) +% ((@as(c_int, p.unk6) *% f) >> 12));

        if (@as(c_int, p.unk8) >= @as(c_int, p.unkA)) {
            p.unkA = 0;
        }
        p.unk0 = 1;
    }
}
