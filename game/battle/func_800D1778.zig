//! Linear interpolation between two SVECTORs by step / steps (src/BATTLE/BATTLE.PRG/5BF94.c).
const v = @import("views_5BF94");
const c = @import("c_5BF94");

/// orig: func_800D1778 (src/BATTLE/BATTLE.PRG/5BF94.c)
/// func_800D169C (the per-axis lerp) from `from` to `to` by step * 8192 / steps into `out`.
pub fn func_800D1778(from: ?*v.SVECTOR, to: ?*v.SVECTOR, steps: c_int, step: c_int, out: ?*v.SVECTOR) callconv(.c) void {
    c.func_800D169C(@ptrCast(from), @ptrCast(to), @divTrunc(step *% (4096 * 2), steps), @ptrCast(out));
}
