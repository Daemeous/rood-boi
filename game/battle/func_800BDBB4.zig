//! Steps a timed interpolation and writes its current position
//! (pc/src/game/BATTLE/BATTLE.PRG/4A0A8.c).
const common = @import("common");
const pad = @import("common").pad;
const psx = common.psx;
const v = @import("views_4A0A8");
const c = @import("c_4A0A8");

/// orig: func_800BDBB4 (pc/src/game/BATTLE/BATTLE.PRG/4A0A8.c)
/// Advances a timed interpolation by half a tick, writes its position, and returns 1 while it runs (0 when idle).
pub fn func_800BDBB4(arg0: ?*v.vs_event_TripleTween) callconv(.c) c_int {
    const p = arg0.?;
    if (p.durationFrames == 0) {
        return 0;
    }

    p.elapsedFrames = psx.conv(i16, @as(c_int, p.elapsedFrames) +% (v.g.vs_gametime_tickspeed().* >> 1));

    const v0: c_int = c.vs_event_evaluateInterpolationCurve(@as(c_int, p.easingMode), @as(c_int, p.elapsedFrames), @as(c_int, p.durationFrames));
    // Scratchpad 0x1F800088: the factor is re-read from memory after each store, as the C does.
    const a0: *v.vs_event_EasedFractionScratch = pad.at(v.vs_event_EasedFractionScratch, 0x88);
    a0.unk0 = v0;
    p.currentValue0 = psx.conv(i16, @as(c_int, p.startValue0) +% ((@as(c_int, p.deltaValue0) *% a0.unk0) >> 12));
    p.currentValue1 = psx.conv(i16, @as(c_int, p.startValue1) +% ((@as(c_int, p.deltaValue1) *% a0.unk0) >> 12));
    p.currentValue2 = psx.conv(i16, @as(c_int, p.startValue2) +% ((@as(c_int, p.deltaValue2) *% a0.unk0) >> 12));

    if (p.elapsedFrames == p.durationFrames) {
        p.durationFrames = 0;
    }
    return 1;
}
