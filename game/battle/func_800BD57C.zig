//! Advances a timed value by half the game tick speed and recomputes its
//! interpolated output (pc/src/game/BATTLE/BATTLE.PRG/4A0A8.c).
const psx = @import("common").psx;
const v = @import("views_4A0A8");
const c = @import("c_4A0A8");

/// orig: func_800BD57C (pc/src/game/BATTLE/BATTLE.PRG/4A0A8.c)
/// While the timer's target (unkA) is positive, steps its elapsed time and sets the output from the curve.
pub fn func_800BD57C(arg0: ?*v.vs_event_AngleTween) callconv(.c) void {
    const p = arg0.?;
    if (@as(c_int, p.durationFrames) > 0) {
        p.elapsedFrames = psx.conv(i16, @as(c_int, p.elapsedFrames) +% (v.g.vs_gametime_tickspeed().* >> 1));

        // The call comes first; the field loads after it, as GCC orders them.
        const f = c.vs_event_evaluateInterpolationCurve(@as(c_int, p.easingMode), @as(c_int, p.elapsedFrames), @as(c_int, p.durationFrames));
        p.angle = psx.conv(i16, @as(c_int, p.startAngle) +% ((@as(c_int, p.deltaAngle) *% f) >> 12));

        if (@as(c_int, p.elapsedFrames) >= @as(c_int, p.durationFrames)) {
            p.durationFrames = 0;
        }
        p.hasChanged = 1;
    }
}
