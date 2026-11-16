//! Copies one sample from each curve of the loaded curve block, at the frame index modulo that curve's length, into vs_effects_curveSamples (pc/src/game/BATTLE/BATTLE.PRG/5BF94.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_5BF94");

/// orig: vs_effects_sampleAllEffectCurves (pc/src/game/BATTLE/BATTLE.PRG/5BF94.c)
/// Samples each curve of the curve block at `arg0` modulo its size into vs_effects_curveSamples[1..].
pub fn vs_effects_sampleAllEffectCurves(arg0: c_int) callconv(.c) void {
    const block = psx.get(v.g.vs_effects_package().*.?.curveBlock).?;
    var i: c_int = 0;
    while (i < @as(c_int, block.count)) : (i +%= 1) {
        const curve = psx.get(psx.at(&v.g.vs_effects_package().*.?.curves, i).*).?;
        const size = psx.at(&block.curveSizes, i).*;
        // arg0 % size: signed remainder, as the C computes it (a zero size traps, as SIGFPE does).
        const sample = psx.at(curve, @rem(arg0, @as(c_int, size))).*;
        psx.at(v.g.vs_effects_curveSamples(), i +% 1).* = @as(c_int, sample);
    }
}
