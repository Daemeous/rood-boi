//! A P-file curve sample (src/BATTLE/BATTLE.PRG/5BF94.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_5BF94");

/// orig: vs_battle_sampleCurve (src/BATTLE/BATTLE.PRG/5BF94.c)
/// Curve `curve_id` (1-based) at `distance` modulo its length, or 0 for curve 0 or past the block's count.
pub fn sampleCurve(curve_id: c_int, distance: c_int) callconv(.c) c_int {
    const pfile = v.g.D_800F569C().*.?;
    const block = psx.get(pfile.curveBlock);
    if (curve_id != 0 and @as(c_int, block.?.count) >= curve_id) {
        const k = curve_id -% 1;
        const samples: [*]u8 = @ptrCast(psx.get(psx.at(&pfile.curves, k).*).?); // curves[32]: the count is not checked against it
        const length: c_int = psx.at(&block.?.curveSizes, k).*;
        return psx.at(samples, @rem(distance, length)).*;
    }
    return 0;
}
